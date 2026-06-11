#define CROW_MAIN
#include "crow.h"
#include "config.h"
#include "data_types.h"
#include "mongodb_manager.h"
#include "ble_data_receiver.h"
#include "data_processor.h"
#include "websocket_manager.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace tcm {

static std::string sensor_data_to_json(const SensorData& d) {
    std::ostringstream oss;
    oss << "{"
        << "\"volunteer_id\":\"" << d.volunteer_id << "\","
        << "\"acupoint_id\":\"" << d.acupoint_id << "\","
        << "\"meridian_id\":\"" << d.meridian_id << "\","
        << "\"timestamp\":" << d.timestamp << ","
        << "\"skin_conductance\":" << d.skin_conductance << ","
        << "\"skin_conductance_prev\":" << d.skin_conductance_prev << ","
        << "\"infrared_temperature\":" << d.infrared_temperature << ","
        << "\"emg_amplitude\":" << d.emg_amplitude << ","
        << "\"emg_frequency\":" << d.emg_frequency << ","
        << "\"is_post_acupuncture\":" << (d.is_post_acupuncture ? "true" : "false") << ","
        << "\"session_id\":\"" << d.session_id << "\""
        << "}";
    return oss.str();
}

struct IngestContext {
    MongoDBManager* db;
    BLEDataReceiver* ble;
    WebSocketManager* ws;
    DataProcessor* processor;
    std::string alerter_url;
    std::string predictor_url;
    std::mutex mutex;
    crow::SimpleApp* app = nullptr;
};

static IngestContext* g_ctx = nullptr;

static void http_post_json(const std::string& url, const std::string& body) {
    auto sep = url.find("://");
    if (sep == std::string::npos) return;
    auto rest = url.substr(sep + 3);
    auto slash = rest.find('/');
    std::string host_port = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    std::string path = (slash == std::string::npos) ? "/" : rest.substr(slash);

    auto colon = host_port.find(':');
    std::string host = (colon == std::string::npos) ? host_port : host_port.substr(0, colon);
    int port = (colon == std::string::npos) ? 80 : std::atoi(host_port.substr(colon + 1).c_str());

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { 
#ifdef _WIN32
        WSACleanup();
#endif
        return; 
    }

    struct hostent* he = gethostbyname(host.c_str());
    if (!he) {
#ifdef _WIN32
        closesocket(sock); WSACleanup();
#else
        close(sock);
#endif
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(sock); WSACleanup();
#else
        close(sock);
#endif
        return;
    }

    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    send(sock, req.str().c_str(), req.str().size(), 0);

    char buf[1024];
    recv(sock, buf, sizeof(buf), 0);

#ifdef _WIN32
    closesocket(sock); WSACleanup();
#else
    close(sock);
#endif
}

static void on_sensor_cb(const SensorData& data) {
    if (!g_ctx) return;
    std::lock_guard<std::mutex> lk(g_ctx->mutex);
    g_ctx->db->insert_sensor_data(data);
    g_ctx->processor->process_sensor_data(data);
    std::string msg = "{\"type\":\"sensor\",\"data\":" + sensor_data_to_json(data) + "}";
    g_ctx->ws->broadcast(msg);

    std::string json_body = sensor_data_to_json(data);
    std::thread([json_body]() {
        http_post_json(g_ctx->alerter_url + "/api/sensor/forward", json_body);
    }).detach();
}

static void on_processed_cb(const SensorData& data, const EfficacyRecord& eff, const PredictionResult& pred) {
    if (!g_ctx) return;
    std::lock_guard<std::mutex> lk(g_ctx->mutex);
    g_ctx->db->insert_efficacy_record(eff);
    g_ctx->db->insert_prediction(pred);
}

} // namespace tcm

int main(int argc, char* argv[]) {
    using namespace tcm;

    std::string config_path = "./config.yaml";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) config_path = argv[++i];
    }

    auto cfg = AppConfig::load_or_default(config_path);

    MongoDBManager db;
    BLEDataReceiver ble;
    DataProcessor processor;
    WebSocketManager ws;

    IngestContext ctx;
    ctx.db = &db;
    ctx.ble = &ble;
    ctx.ws = &ws;
    ctx.processor = &processor;
    ctx.alerter_url = cfg.alerter_url();
    ctx.predictor_url = cfg.predictor_url();
    g_ctx = &ctx;

    std::cout << "[INGEST] Connecting MongoDB..." << std::endl;
    db.initialize(cfg.mongodb.uri, cfg.mongodb.db_name);
    db.ensure_indexes();

    processor.set_efficacy_callback(on_processed_cb);
    ble.set_data_callback(on_sensor_cb);

    std::cout << "[INGEST] Starting BLE on UDP " << cfg.ingest.ble_udp_port << "..." << std::endl;
    ble.start(cfg.ingest.ble_udp_port);

    crow::SimpleApp app;
    ctx.app = &app;

    CROW_ROUTE(app, "/api/health")([]() {
        crow::json::wvalue r;
        r["status"] = "ok";
        r["service"] = "ingest";
        return r;
    });

    CROW_ROUTE(app, "/api/sensor/ingest")
    .methods(crow::HTTPMethod::POST)([&ble](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "{\"error\":\"invalid json\"}");
        SensorData d;
        d.volunteer_id = std::string(body["volunteer_id"].s());
        d.acupoint_id = std::string(body["acupoint_id"].s());
        d.meridian_id = body.has("meridian_id") ? std::string(body["meridian_id"].s()) : "";
        d.timestamp = body.has("timestamp") ? (uint64_t)body["timestamp"].i() :
            (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        d.skin_conductance = body["skin_conductance"].d();
        d.skin_conductance_prev = body.has("skin_conductance_prev") ? body["skin_conductance_prev"].d() : d.skin_conductance;
        d.infrared_temperature = body["infrared_temperature"].d();
        d.emg_amplitude = body["emg_amplitude"].d();
        d.emg_frequency = body["emg_frequency"].d();
        d.is_post_acupuncture = body.has("is_post_acupuncture") && (bool)body["is_post_acupuncture"].b();
        d.session_id = body.has("session_id") ? std::string(body["session_id"].s()) : "default";
        ble.inject_simulated_data(d);
        return crow::response(200, "{\"status\":\"ok\"}");
    });

    CROW_ROUTE(app, "/api/sensor/query")
    .methods(crow::HTTPMethod::POST)([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "{\"error\":\"invalid json\"}");
        std::string vid = body.has("volunteer_id") ? std::string(body["volunteer_id"].s()) : "";
        std::string aid = body.has("acupoint_id") ? std::string(body["acupoint_id"].s()) : "";
        uint64_t start = body.has("start_time") ? (uint64_t)body["start_time"].i() : 0;
        uint64_t end = body.has("end_time") ? (uint64_t)body["end_time"].i() : (uint64_t)-1;
        int limit = body.has("limit") ? (int)body["limit"].i() : 10000;
        auto data = db.query_sensor_data(vid, aid, start, end, limit);
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < data.size(); ++i) {
            if (i > 0) oss << ",";
            oss << sensor_data_to_json(data[i]);
        }
        oss << "]";
        return crow::response(200, "application/json", oss.str());
    });

    CROW_ROUTE(app, "/api/session/start")
    .methods(crow::HTTPMethod::POST)([&processor](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "{\"error\":\"invalid json\"}");
        std::string vid = std::string(body["volunteer_id"].s());
        std::string sid = std::string(body["session_id"].s());
        processor.start_session(vid, sid);
        return crow::response(200, "{\"status\":\"ok\"}");
    });

    CROW_ROUTE(app, "/api/session/end")
    .methods(crow::HTTPMethod::POST)([&processor](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "{\"error\":\"invalid json\"}");
        std::string vid = std::string(body["volunteer_id"].s());
        std::string sid = std::string(body["session_id"].s());
        processor.end_session(vid, sid);
        auto summary = processor.compute_efficacy_summary(vid, sid);
        std::ostringstream oss;
        oss << "{\"deqi_intensity\":" << summary.deqi_intensity
            << ",\"pain_relief_rate\":" << summary.pain_relief_rate
            << ",\"efficacy_text\":\"" << summary.efficacy_text << "\"}";
        return crow::response(200, "application/json", oss.str());
    });

    CROW_ROUTE(app, "/api/efficacy/query")
    .methods(crow::HTTPMethod::POST)([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "{\"error\":\"invalid json\"}");
        std::string vid = body.has("volunteer_id") ? std::string(body["volunteer_id"].s()) : "";
        uint64_t start = body.has("start_time") ? (uint64_t)body["start_time"].i() : 0;
        uint64_t end = body.has("end_time") ? (uint64_t)body["end_time"].i() : (uint64_t)-1;
        auto recs = db.query_efficacy_records(vid, start, end);
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < recs.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{\"volunteer_id\":\"" << recs[i].volunteer_id << "\","
                << "\"session_id\":\"" << recs[i].session_id << "\","
                << "\"acupoint_id\":\"" << recs[i].acupoint_id << "\","
                << "\"timestamp\":" << recs[i].timestamp << ","
                << "\"deqi_intensity\":" << recs[i].deqi_intensity << ","
                << "\"pain_relief_rate\":" << recs[i].pain_relief_rate << ","
                << "\"efficacy_text\":\"" << recs[i].efficacy_text << "\"}";
        }
        oss << "]";
        return crow::response(200, "application/json", oss.str());
    });

    CROW_ROUTE(app, "/ws").websocket()
        .onopen([&ws](crow::websocket::connection& conn) {
            ws.add_connection(&conn);
            std::cout << "[INGEST-WS] New connection" << std::endl;
        })
        .onclose([&ws](crow::websocket::connection& conn, const std::string& reason) {
            std::cout << "[INGEST-WS] Closed" << std::endl;
        })
        .onmessage([&ws](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
        });

    std::cout << "[INGEST] HTTP on port " << cfg.ingest.http_port
              << ", BLE on UDP " << cfg.ingest.ble_udp_port << std::endl;
    app.loglevel(crow::LogLevel::Warning).port(cfg.ingest.http_port).multithreaded().run();

    ble.stop();
    db.shutdown();
    return 0;
}

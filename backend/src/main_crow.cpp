#define CROW_ENABLE_SSL
#define CROW_MAIN
#include "crow.h"
#include "http_server.h"
#include "data_types.h"
#include "mongodb_manager.h"
#include "random_forest_model.h"
#include "meridian_network_analyzer.h"
#include "anomaly_detector.h"
#include "websocket_manager.h"
#include "data_processor.h"
#include "dingtalk_notifier.h"
#include "ble_data_receiver.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <mutex>
#include <functional>

namespace tcm {

crow::SimpleApp* g_crow_app = nullptr;

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

static std::string alert_to_json(const Alert& a) {
    std::ostringstream oss;
    oss << "{"
        << "\"id\":\"" << a.id << "\","
        << "\"timestamp\":" << a.timestamp << ","
        << "\"volunteer_id\":\"" << a.volunteer_id << "\","
        << "\"acupoint_id\":\"" << a.acupoint_id << "\","
        << "\"alert_type\":\"" << a.alert_type << "\","
        << "\"message\":\"" << a.message << "\","
        << "\"value\":" << a.value << ","
        << "\"threshold\":" << a.threshold << ","
        << "\"acknowledged\":" << (a.acknowledged ? "true" : "false")
        << "}";
    return oss.str();
}

static std::string prediction_to_json(const PredictionResult& p) {
    std::ostringstream oss;
    oss << "{"
        << "\"volunteer_id\":\"" << p.volunteer_id << "\","
        << "\"session_id\":\"" << p.session_id << "\","
        << "\"timestamp\":" << p.timestamp << ","
        << "\"predicted_deqi\":" << p.predicted_deqi << ","
        << "\"predicted_pain_relief\":" << p.predicted_pain_relief << ","
        << "\"confidence\":" << p.confidence << ","
        << "\"feature_importance\":[";
    for (size_t i = 0; i < p.feature_importance.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << p.feature_importance[i] << "\"";
    }
    oss << "]}";
    return oss.str();
}

static std::string acupoint_to_json(const AcupointInfo& ap) {
    std::ostringstream oss;
    oss << "{"
        << "\"id\":\"" << ap.id << "\","
        << "\"name\":\"" << ap.name << "\","
        << "\"pinyin\":\"" << ap.pinyin << "\","
        << "\"meridian_id\":\"" << ap.meridian_id << "\","
        << "\"x\":" << ap.x << ","
        << "\"y\":" << ap.y << ","
        << "\"z\":" << ap.z << ","
        << "\"description\":\"" << ap.description << "\","
        << "\"indications\":[";
    for (size_t i = 0; i < ap.indications.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << ap.indications[i] << "\"";
    }
    oss << "]}";
    return oss.str();
}

static std::string meridian_to_json(const MeridianInfo& m) {
    std::ostringstream oss;
    oss << "{"
        << "\"id\":\"" << m.id << "\","
        << "\"name\":\"" << m.name << "\","
        << "\"pinyin\":\"" << m.pinyin << "\","
        << "\"element\":\"" << m.element << "\","
        << "\"path_points\":[";
    for (size_t i = 0; i < m.path_points.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "[" << m.path_points[i].first << "," << m.path_points[i].second << "]";
    }
    oss << "],\"acupoint_ids\":[";
    for (size_t i = 0; i < m.acupoint_ids.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << m.acupoint_ids[i] << "\"";
    }
    oss << "]}";
    return oss.str();
}

struct GlobalServer {
    MongoDBManager* db;
    BLEDataReceiver* ble;
    RandomForestModel* rf;
    MeridianNetworkAnalyzer* network;
    AnomalyDetector* anomaly;
    DingTalkNotifier* dingtalk;
    WebSocketManager* ws;
    DataProcessor* processor;
    std::mutex mutex;
};

static GlobalServer* g_srv = nullptr;

static void on_sensor_cb(const SensorData& data) {
    if (!g_srv) return;
    std::lock_guard<std::mutex> lk(g_srv->mutex);
    g_srv->db->insert_sensor_data(data);
    g_srv->anomaly->process_sensor_data(data);
    g_srv->processor->process_sensor_data(data);
    std::string msg = "{\"type\":\"sensor\",\"data\":" + sensor_data_to_json(data) + "}";
    g_srv->ws->broadcast(msg);
}

static void on_alert_cb(const Alert& alert) {
    if (!g_srv) return;
    std::lock_guard<std::mutex> lk(g_srv->mutex);
    g_srv->db->insert_alert(alert);
    g_srv->dingtalk->send_alert(alert);
    std::string msg = "{\"type\":\"alert\",\"data\":" + alert_to_json(alert) + "}";
    g_srv->ws->broadcast(msg);
}

static void on_processed_cb(const SensorData& data, const EfficacyRecord& eff, const PredictionResult& pred) {
    if (!g_srv) return;
    std::lock_guard<std::mutex> lk(g_srv->mutex);
    g_srv->db->insert_efficacy_record(eff);
    g_srv->db->insert_prediction(pred);
    std::string msg1 = "{\"type\":\"prediction\",\"data\":" + prediction_to_json(pred) + "}";
    g_srv->ws->broadcast(msg1);
}

} // namespace tcm

int main(int argc, char* argv[]) {
    using namespace tcm;

    int port = 8080;
    std::string mongo_uri = "mongodb://localhost:27017";
    std::string db_name = "tcm_acupuncture";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (arg == "--mongodb" && i + 1 < argc) mongo_uri = argv[++i];
        else if (arg == "--db" && i + 1 < argc) db_name = argv[++i];
    }

    MongoDBManager db;
    BLEDataReceiver ble;
    RandomForestModel rf;
    MeridianNetworkAnalyzer network;
    AnomalyDetector anomaly;
    DingTalkNotifier dingtalk;
    WebSocketManager ws;
    DataProcessor processor;

    GlobalServer srv{&db, &ble, &rf, &network, &anomaly, &dingtalk, &ws, &processor, {}};
    g_srv = &srv;

    std::cout << "[TCM] 连接MongoDB..." << std::endl;
    db.initialize(mongo_uri, db_name);
    db.ensure_indexes();

    std::cout << "[TCM] 加载随机森林模型..." << std::endl;
    rf.load_or_initialize("./rf_model.bin");

    std::cout << "[TCM] 初始化经络网络..." << std::endl;
    network.initialize();

    anomaly.set_alert_callback(on_alert_cb);
    ble.set_data_callback(on_sensor_cb);
    processor.set_efficacy_callback(on_processed_cb);

    std::cout << "[TCM] 启动BLE接收端口 8081..." << std::endl;
    ble.start(8081);

    crow::SimpleApp app;
    g_crow_app = &app;

    CROW_ROUTE(app, "/")([]() {
        return crow::response(200, "text/html",
            "<!DOCTYPE html><html><head><meta charset='utf-8'><title>TCM System</title>"
            "<meta http-equiv='refresh' content='0;url=/static/index.html'></head>"
            "<body><a href='/static/index.html'>进入系统</a></body></html>");
    });

    CROW_ROUTE(app, "/api/health")([]() {
        crow::json::wvalue r;
        r["status"] = "ok";
        r["service"] = "tcm_acupuncture_backend";
        r["version"] = "1.0.0";
        return r;
    });

    CROW_ROUTE(app, "/api/acupoints")([&db]() {
        auto pts = db.get_all_acupoints();
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < pts.size(); ++i) {
            if (i > 0) oss << ",";
            oss << acupoint_to_json(pts[i]);
        }
        oss << "]";
        return crow::response(200, "application/json", oss.str());
    });

    CROW_ROUTE(app, "/api/meridians")([&db]() {
        auto ms = db.get_all_meridians();
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < ms.size(); ++i) {
            if (i > 0) oss << ",";
            oss << meridian_to_json(ms[i]);
        }
        oss << "]";
        return crow::response(200, "application/json", oss.str());
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
            oss << "{"
                << "\"volunteer_id\":\"" << recs[i].volunteer_id << "\","
                << "\"session_id\":\"" << recs[i].session_id << "\","
                << "\"acupoint_id\":\"" << recs[i].acupoint_id << "\","
                << "\"timestamp\":" << recs[i].timestamp << ","
                << "\"deqi_intensity\":" << recs[i].deqi_intensity << ","
                << "\"pain_relief_rate\":" << recs[i].pain_relief_rate << ","
                << "\"efficacy_text\":\"" << recs[i].efficacy_text << "\""
                << "}";
        }
        oss << "]";
        return crow::response(200, "application/json", oss.str());
    });

    CROW_ROUTE(app, "/api/alerts")
    .methods(crow::HTTPMethod::GET)([&db](const crow::request& req) {
        uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto alerts = db.query_alerts(now - 3600000, now, false);
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < alerts.size(); ++i) {
            if (i > 0) oss << ",";
            oss << alert_to_json(alerts[i]);
        }
        oss << "]";
        return crow::response(200, "application/json", oss.str());
    });

    CROW_ROUTE(app, "/api/predict")
    .methods(crow::HTTPMethod::POST)([&rf, &processor](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "{\"error\":\"invalid json\"}");
        std::string vid = std::string(body["volunteer_id"].s());
        std::string sid = std::string(body["session_id"].s());
        std::vector<double> features;
        if (body.has("features")) {
            for (const auto& v : body["features"]) features.push_back(v.d());
        }
        auto pred = rf.predict(vid, sid, features);
        return crow::response(200, "application/json", prediction_to_json(pred));
    });

    CROW_ROUTE(app, "/api/network/metrics")([&network]() {
        auto metrics = network.compute_all_metrics();
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < metrics.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{"
                << "\"acupoint_id\":\"" << metrics[i].acupoint_id << "\","
                << "\"degree_centrality\":" << metrics[i].degree_centrality << ","
                << "\"betweenness_centrality\":" << metrics[i].betweenness_centrality << ","
                << "\"closeness_centrality\":" << metrics[i].closeness_centrality << ","
                << "\"clustering_coefficient\":" << metrics[i].clustering_coefficient
                << "}";
        }
        oss << "]";
        return crow::response(200, "application/json", oss.str());
    });

    CROW_ROUTE(app, "/api/network/adjacency")([&network]() {
        auto adj = network.get_adjacency_matrix();
        std::ostringstream oss;
        oss << "[";
        bool first = true;
        for (const auto& kv : adj) {
            if (!first) oss << ",";
            first = false;
            oss << "{\"a\":\"" << kv.first.first << "\",\"b\":\"" << kv.first.second
                << "\",\"weight\":" << kv.second << "}";
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
        oss << "{"
            << "\"deqi_intensity\":" << summary.deqi_intensity << ","
            << "\"pain_relief_rate\":" << summary.pain_relief_rate << ","
            << "\"efficacy_text\":\"" << summary.efficacy_text << "\""
            << "}";
        return crow::response(200, "application/json", oss.str());
    });

    CROW_ROUTE(app, "/ws").websocket()
        .onopen([&ws](crow::websocket::connection& conn) {
            auto id = ws.add_connection(&conn);
            std::cout << "[WS] 新连接, ID=" << id << std::endl;
        })
        .onclose([&ws](crow::websocket::connection& conn, const std::string& reason) {
            std::cout << "[WS] 连接关闭" << std::endl;
        })
        .onmessage([&ws](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
            std::cout << "[WS] 收到消息: " << data << std::endl;
        });

    std::cout << "[TCM] 启动HTTP服务端口 " << port << "..." << std::endl;
    std::cout << "[TCM] 访问 http://localhost:" << port << "/static/index.html" << std::endl;

    app.loglevel(crow::LogLevel::Warning).port(port).multithreaded().run();

    ble.stop();
    db.shutdown();
    return 0;
}

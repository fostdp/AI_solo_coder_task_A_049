#define CROW_MAIN
#include "crow.h"
#include "config.h"
#include "data_types.h"
#include "anomaly_detector.h"
#include "dingtalk_notifier.h"
#include "mongodb_manager.h"
#include <iostream>
#include <sstream>
#include <mutex>

namespace tcm {

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

struct AlerterContext {
    AnomalyDetector* anomaly;
    DingTalkNotifier* dingtalk;
    MongoDBManager* db;
    std::mutex mutex;
};

static AlerterContext* g_ctx = nullptr;

static void on_alert_cb(const Alert& alert) {
    if (!g_ctx) return;
    std::lock_guard<std::mutex> lk(g_ctx->mutex);
    g_ctx->db->insert_alert(alert);
    g_ctx->dingtalk->send_alert(alert);
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
    AnomalyDetector anomaly;
    DingTalkNotifier dingtalk;

    AlerterContext ctx;
    ctx.db = &db;
    ctx.anomaly = &anomaly;
    ctx.dingtalk = &dingtalk;
    g_ctx = &ctx;

    std::cout << "[ALERTER] Connecting MongoDB..." << std::endl;
    db.initialize(cfg.mongodb.uri, cfg.mongodb.db_name);

    anomaly.set_alert_callback(on_alert_cb);

    if (cfg.alerter_rules.dingtalk_enabled) {
        dingtalk.initialize(cfg.alerter_rules.dingtalk_webhook_url,
                          cfg.alerter_rules.dingtalk_secret);
        std::cout << "[ALERTER] DingTalk enabled" << std::endl;
    } else {
        std::cout << "[ALERTER] DingTalk disabled" << std::endl;
    }

    crow::SimpleApp app;

    CROW_ROUTE(app, "/api/health")([]() {
        crow::json::wvalue r;
        r["status"] = "ok";
        r["service"] = "alerter";
        return r;
    });

    CROW_ROUTE(app, "/api/sensor/forward")
    .methods(crow::HTTPMethod::POST)([&anomaly](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "{\"error\":\"invalid json\"}");
        SensorData d;
        d.volunteer_id = std::string(body["volunteer_id"].s());
        d.acupoint_id = std::string(body["acupoint_id"].s());
        d.meridian_id = body.has("meridian_id") ? std::string(body["meridian_id"].s()) : "";
        d.timestamp = body.has("timestamp") ? (uint64_t)body["timestamp"].i() : 0;
        d.skin_conductance = body.has("skin_conductance") ? body["skin_conductance"].d() : 0;
        d.skin_conductance_prev = body.has("skin_conductance_prev") ? body["skin_conductance_prev"].d() : d.skin_conductance;
        d.infrared_temperature = body.has("infrared_temperature") ? body["infrared_temperature"].d() : 36.5;
        d.emg_amplitude = body.has("emg_amplitude") ? body["emg_amplitude"].d() : 0;
        d.emg_frequency = body.has("emg_frequency") ? body["emg_frequency"].d() : 0;
        d.is_post_acupuncture = body.has("is_post_acupuncture") && body["is_post_acupuncture"].b();
        d.session_id = body.has("session_id") ? std::string(body["session_id"].s()) : "";
        anomaly.process_sensor_data(d);
        return crow::response(200, "{\"status\":\"ok\"}");
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

    CROW_ROUTE(app, "/api/alerts/acknowledge")
    .methods(crow::HTTPMethod::POST)([&db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "{\"error\":\"invalid json\"}");
        std::string alert_id = std::string(body["id"].s());
        db.acknowledge_alert(alert_id);
        return crow::response(200, "{\"status\":\"ok\"}");
    });

    std::cout << "[ALERTER] HTTP on port " << cfg.alerter.http_port << std::endl;
    app.loglevel(crow::LogLevel::Warning).port(cfg.alerter.http_port).multithreaded().run();

    db.shutdown();
    return 0;
}

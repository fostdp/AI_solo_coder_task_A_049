#define CROW_MAIN
#include "crow.h"
#include "config.h"
#include "data_types.h"
#include "random_forest_model.h"
#include <iostream>
#include <sstream>
#include <mutex>

namespace tcm {

struct PredictorContext {
    RandomForestModel* rf;
    std::mutex mutex;
};

static PredictorContext* g_ctx = nullptr;

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

} // namespace tcm

int main(int argc, char* argv[]) {
    using namespace tcm;

    std::string config_path = "./config.yaml";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) config_path = argv[++i];
    }

    auto cfg = AppConfig::load_or_default(config_path);

    RandomForestModel rf;
    PredictorContext ctx;
    ctx.rf = &rf;
    g_ctx = &ctx;

    std::cout << "[PREDICTOR] Loading model from " << cfg.predictor_model.model_path << "..." << std::endl;
    rf.load_or_initialize(cfg.predictor_model.model_path);

    crow::SimpleApp app;

    CROW_ROUTE(app, "/api/health")([]() {
        crow::json::wvalue r;
        r["status"] = "ok";
        r["service"] = "predictor";
        return r;
    });

    CROW_ROUTE(app, "/api/predict")
    .methods(crow::HTTPMethod::POST)([&rf](const crow::request& req) {
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

    CROW_ROUTE(app, "/api/sensor/forward")
    .methods(crow::HTTPMethod::POST)([&rf](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "{\"error\":\"invalid json\"}");
        std::string vid = std::string(body["volunteer_id"].s());
        std::vector<double> features;
        if (body.has("skin_conductance")) features.push_back(body["skin_conductance"].d());
        if (body.has("infrared_temperature")) features.push_back(body["infrared_temperature"].d());
        if (body.has("emg_amplitude")) features.push_back(body["emg_amplitude"].d());
        if (body.has("emg_frequency")) features.push_back(body["emg_frequency"].d());
        if (!features.empty()) {
            auto stats = rf.get_volunteer_stats(vid);
            if (stats.sample_count < 3) {
                for (size_t i = 0; i < features.size() && i < stats.feature_means.size(); ++i) {
                    stats.feature_means[i] = (stats.feature_means[i] * stats.sample_count + features[i]) / (stats.sample_count + 1);
                    stats.feature_stds[i] = 1.0;
                    stats.sample_count++;
                }
            }
        }
        return crow::response(200, "{\"status\":\"ok\"}");
    });

    CROW_ROUTE(app, "/api/features")([&rf]() {
        auto imp = rf.get_feature_importance();
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < imp.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{\"index\":" << i << ",\"importance\":" << imp[i] << "}";
        }
        oss << "]";
        return crow::response(200, "application/json", oss.str());
    });

    std::cout << "[PREDICTOR] HTTP on port " << cfg.predictor.http_port << std::endl;
    app.loglevel(crow::LogLevel::Warning).port(cfg.predictor.http_port).multithreaded().run();

    return 0;
}

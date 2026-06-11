#define CROW_MAIN
#include "crow.h"
#include "config.h"
#include "data_types.h"
#include "meridian_network_analyzer.h"
#include "mongodb_manager.h"
#include <iostream>
#include <sstream>

namespace tcm {

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
    MeridianNetworkAnalyzer network;

    std::cout << "[TOPOLOGY] Connecting MongoDB..." << std::endl;
    db.initialize(cfg.mongodb.uri, cfg.mongodb.db_name);

    std::cout << "[TOPOLOGY] Initializing meridian network..." << std::endl;
    network.initialize();

    crow::SimpleApp app;

    CROW_ROUTE(app, "/api/health")([]() {
        crow::json::wvalue r;
        r["status"] = "ok";
        r["service"] = "topology";
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

    CROW_ROUTE(app, "/api/network/path")
    .methods(crow::HTTPMethod::POST)([&network](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "{\"error\":\"invalid json\"}");
        std::string from = std::string(body["from"].s());
        std::string to = std::string(body["to"].s());
        auto pathResult = network.find_optimal_path(from, to);
        std::ostringstream oss;
        oss << "{\"path\":[";
        for (size_t i = 0; i < pathResult.path.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << pathResult.path[i] << "\"";
        }
        oss << "],\"length\":" << pathResult.path.size() << "}";
        return crow::response(200, "application/json", oss.str());
    });

    std::cout << "[TOPOLOGY] HTTP on port " << cfg.topology.http_port << std::endl;
    app.loglevel(crow::LogLevel::Warning).port(cfg.topology.http_port).multithreaded().run();

    db.shutdown();
    return 0;
}

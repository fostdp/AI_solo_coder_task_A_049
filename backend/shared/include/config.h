#pragma once
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace tcm {

struct ServiceEndpoint {
    std::string host = "0.0.0.0";
    int http_port = 8080;
    int ble_udp_port = 8081;
};

struct MongoDBConfig {
    std::string uri = "mongodb://localhost:27017";
    std::string db_name = "tcm_acupuncture";
    int batch_size = 1000;
    int flush_interval_ms = 50;
    int max_queue_size = 100000;
};

struct BLEConfig {
    int reconnect_initial_delay_ms = 1000;
    int reconnect_max_delay_ms = 32000;
    double reconnect_backoff = 2.0;
    int heartbeat_interval_ms = 5000;
    int heartbeat_timeout_ms = 12000;
    int offline_cache_max = 10000;
};

struct PredictorConfig {
    std::string model_path = "./rf_model.bin";
    int num_trees = 50;
    int max_depth = 15;
    int min_samples_split = 5;
    bool use_volunteer_normalization = true;
    bool use_group_kfold = true;
};

struct AlerterConfig {
    double conductance_drop_threshold = 30.0;
    double temperature_high = 38.0;
    double temperature_low = 35.0;
    int window_size = 50;
    std::string dingtalk_webhook_url;
    std::string dingtalk_secret;
    bool dingtalk_enabled = false;
};

struct SimulatorConfig {
    std::string server_host = "127.0.0.1";
    int udp_port = 8081;
    std::string http_url = "http://127.0.0.1:8080";
    int num_volunteers = 30;
    int interval_ms = 100;
    bool inject_anomalies = true;
    double anomaly_probability = 0.005;
};

struct AppConfig {
    ServiceEndpoint ingest;
    ServiceEndpoint predictor;
    ServiceEndpoint topology;
    ServiceEndpoint alerter;
    MongoDBConfig mongodb;
    BLEConfig ble;
    PredictorConfig predictor_model;
    AlerterConfig alerter_rules;
    SimulatorConfig simulator;

    std::string ingest_url() const {
        return "http://127.0.0.1:" + std::to_string(ingest.http_port);
    }
    std::string predictor_url() const {
        return "http://127.0.0.1:" + std::to_string(predictor.http_port);
    }
    std::string topology_url() const {
        return "http://127.0.0.1:" + std::to_string(topology.http_port);
    }
    std::string alerter_url() const {
        return "http://127.0.0.1:" + std::to_string(alerter.http_port);
    }

    static AppConfig load(const std::string& path);
    static AppConfig load_or_default(const std::string& path);
};

class SimpleYAML {
public:
    using Map = std::map<std::string, std::string>;
    using Section = std::map<std::string, Map>;

    static Section parse(const std::string& content) {
        Section result;
        std::vector<std::pair<int, std::string>> section_stack;
        std::string current_section;
        std::istringstream stream(content);
        std::string line;

        while (std::getline(stream, line)) {
            int indent = 0;
            for (size_t i = 0; i < line.size(); ++i) {
                if (line[i] == ' ') indent++;
                else if (line[i] == '\t') indent += 2;
                else break;
            }
            auto trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            auto colon = trimmed.find(':');
            if (colon == std::string::npos) continue;

            std::string key = trim(trimmed.substr(0, colon));
            std::string val = trim(trimmed.substr(colon + 1));

            while (!section_stack.empty() && section_stack.back().first >= indent) {
                section_stack.pop_back();
            }

            if (val.empty()) {
                std::string full;
                for (auto& p : section_stack) {
                    if (!full.empty()) full += ".";
                    full += p.second;
                }
                if (!full.empty()) full += ".";
                full += key;
                current_section = full;
                result[current_section] = Map();
                section_stack.push_back({indent, key});
            } else {
                if (!val.empty() && val[0] == '"') {
                    val = unquote(val);
                }
                result[current_section][key] = val;
            }
        }
        return result;
    }

    static std::string get(const Section& s, const std::string& section,
                           const std::string& key, const std::string& def = "") {
        auto it = s.find(section);
        if (it == s.end()) return def;
        auto it2 = it->second.find(key);
        if (it2 == it->second.end()) return def;
        return it2->second;
    }

    static int get_int(const Section& s, const std::string& section,
                       const std::string& key, int def = 0) {
        auto v = get(s, section, key);
        return v.empty() ? def : std::atoi(v.c_str());
    }

    static double get_double(const Section& s, const std::string& section,
                             const std::string& key, double def = 0.0) {
        auto v = get(s, section, key);
        return v.empty() ? def : std::atof(v.c_str());
    }

    static bool get_bool(const Section& s, const std::string& section,
                         const std::string& key, bool def = false) {
        auto v = get(s, section, key);
        if (v == "true" || v == "1" || v == "yes") return true;
        if (v == "false" || v == "0" || v == "no") return false;
        return def;
    }

private:
    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static std::string unquote(const std::string& s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        return s;
    }
};

inline AppConfig AppConfig::load(const std::string& path) {
    AppConfig cfg;
    std::ifstream f(path);
    if (!f.is_open()) return cfg;

    std::stringstream buf;
    buf << f.rdbuf();
    auto s = SimpleYAML::parse(buf.str());

    cfg.ingest.host = SimpleYAML::get(s, "services.ingest", "host", "0.0.0.0");
    cfg.ingest.http_port = SimpleYAML::get_int(s, "services.ingest", "http_port", 8080);
    cfg.ingest.ble_udp_port = SimpleYAML::get_int(s, "services.ingest", "ble_udp_port", 8081);

    cfg.predictor.host = SimpleYAML::get(s, "services.predictor", "host", "0.0.0.0");
    cfg.predictor.http_port = SimpleYAML::get_int(s, "services.predictor", "http_port", 8082);

    cfg.topology.host = SimpleYAML::get(s, "services.topology", "host", "0.0.0.0");
    cfg.topology.http_port = SimpleYAML::get_int(s, "services.topology", "http_port", 8083);

    cfg.alerter.host = SimpleYAML::get(s, "services.alerter", "host", "0.0.0.0");
    cfg.alerter.http_port = SimpleYAML::get_int(s, "services.alerter", "http_port", 8084);

    cfg.mongodb.uri = SimpleYAML::get(s, "mongodb", "uri", "mongodb://localhost:27017");
    cfg.mongodb.db_name = SimpleYAML::get(s, "mongodb", "db_name", "tcm_acupuncture");
    cfg.mongodb.batch_size = SimpleYAML::get_int(s, "mongodb", "batch_size", 1000);
    cfg.mongodb.flush_interval_ms = SimpleYAML::get_int(s, "mongodb", "flush_interval_ms", 50);
    cfg.mongodb.max_queue_size = SimpleYAML::get_int(s, "mongodb", "max_queue_size", 100000);

    cfg.ble.reconnect_initial_delay_ms = SimpleYAML::get_int(s, "ble", "reconnect_initial_delay_ms", 1000);
    cfg.ble.reconnect_max_delay_ms = SimpleYAML::get_int(s, "ble", "reconnect_max_delay_ms", 32000);
    cfg.ble.reconnect_backoff = SimpleYAML::get_double(s, "ble", "reconnect_backoff", 2.0);
    cfg.ble.heartbeat_interval_ms = SimpleYAML::get_int(s, "ble", "heartbeat_interval_ms", 5000);
    cfg.ble.heartbeat_timeout_ms = SimpleYAML::get_int(s, "ble", "heartbeat_timeout_ms", 12000);
    cfg.ble.offline_cache_max = SimpleYAML::get_int(s, "ble", "offline_cache_max", 10000);

    cfg.predictor_model.model_path = SimpleYAML::get(s, "predictor", "model_path", "./rf_model.bin");
    cfg.predictor_model.num_trees = SimpleYAML::get_int(s, "predictor", "num_trees", 50);
    cfg.predictor_model.max_depth = SimpleYAML::get_int(s, "predictor", "max_depth", 15);
    cfg.predictor_model.min_samples_split = SimpleYAML::get_int(s, "predictor", "min_samples_split", 5);
    cfg.predictor_model.use_volunteer_normalization = SimpleYAML::get_bool(s, "predictor", "use_volunteer_normalization", true);
    cfg.predictor_model.use_group_kfold = SimpleYAML::get_bool(s, "predictor", "use_group_kfold", true);

    cfg.alerter_rules.conductance_drop_threshold = SimpleYAML::get_double(s, "alerter", "conductance_drop_threshold", 30.0);
    cfg.alerter_rules.temperature_high = SimpleYAML::get_double(s, "alerter", "temperature_high", 38.0);
    cfg.alerter_rules.temperature_low = SimpleYAML::get_double(s, "alerter", "temperature_low", 35.0);
    cfg.alerter_rules.window_size = SimpleYAML::get_int(s, "alerter", "window_size", 50);
    cfg.alerter_rules.dingtalk_webhook_url = SimpleYAML::get(s, "alerter", "dingtalk_webhook_url", "");
    cfg.alerter_rules.dingtalk_secret = SimpleYAML::get(s, "alerter", "dingtalk_secret", "");
    cfg.alerter_rules.dingtalk_enabled = SimpleYAML::get_bool(s, "alerter", "dingtalk_enabled", false);

    cfg.simulator.server_host = SimpleYAML::get(s, "simulator", "server_host", "127.0.0.1");
    cfg.simulator.udp_port = SimpleYAML::get_int(s, "simulator", "udp_port", 8081);
    cfg.simulator.http_url = SimpleYAML::get(s, "simulator", "http_url", "http://127.0.0.1:8080");
    cfg.simulator.num_volunteers = SimpleYAML::get_int(s, "simulator", "num_volunteers", 30);
    cfg.simulator.interval_ms = SimpleYAML::get_int(s, "simulator", "interval_ms", 100);
    cfg.simulator.inject_anomalies = SimpleYAML::get_bool(s, "simulator", "inject_anomalies", true);
    cfg.simulator.anomaly_probability = SimpleYAML::get_double(s, "simulator", "anomaly_probability", 0.005);

    return cfg;
}

inline AppConfig AppConfig::load_or_default(const std::string& path) {
    try {
        auto cfg = load(path);
        std::cout << "[CONFIG] Loaded from " << path << std::endl;
        return cfg;
    } catch (...) {
        std::cerr << "[CONFIG] Failed to load " << path << ", using defaults" << std::endl;
        return AppConfig();
    }
}

} // namespace tcm

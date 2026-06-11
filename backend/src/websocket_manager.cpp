#include "websocket_manager.h"
#include <iostream>
#include <sstream>

namespace tcm {

WebSocketManager::WebSocketManager()
    : next_id_(1) {
}

WebSocketManager::~WebSocketManager() {
}

WebSocketManager::ConnectionId WebSocketManager::add_connection(void* conn_handle) {
    std::lock_guard<std::mutex> lk(mutex_);
    ConnectionId id = next_id_++;
    connections_[id] = conn_handle;
    return id;
}

void WebSocketManager::remove_connection(ConnectionId id) {
    std::lock_guard<std::mutex> lk(mutex_);
    connections_.erase(id);
}

void WebSocketManager::set_message_handler(MessageHandler handler) {
    std::lock_guard<std::mutex> lk(mutex_);
    message_handler_ = std::move(handler);
}

std::string WebSocketManager::build_json_message(const std::string& type, const std::string& payload) {
    return "{\"type\":\"" + type + "\",\"data\":" + payload + "}";
}

bool WebSocketManager::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (const auto& kv : connections_) {
        try {
            auto* conn = static_cast<crow::websocket::connection*>(kv.second);
            if (conn) conn->send_text(message);
        } catch (...) {}
    }
    return true;
}

bool WebSocketManager::send_to_connection(ConnectionId id, const std::string& message) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = connections_.find(id);
    if (it == connections_.end()) return false;
    try {
        auto* conn = static_cast<crow::websocket::connection*>(it->second);
        if (conn) conn->send_text(message);
        return true;
    } catch (...) {
        return false;
    }
}

static std::string sd_to_json(const SensorData& d) {
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

static std::string al_to_json(const Alert& a) {
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

static std::string pr_to_json(const PredictionResult& p) {
    std::ostringstream oss;
    oss << "{"
        << "\"volunteer_id\":\"" << p.volunteer_id << "\","
        << "\"session_id\":\"" << p.session_id << "\","
        << "\"timestamp\":" << p.timestamp << ","
        << "\"predicted_deqi\":" << p.predicted_deqi << ","
        << "\"predicted_pain_relief\":" << p.predicted_pain_relief << ","
        << "\"confidence\":" << p.confidence
        << "}";
    return oss.str();
}

static std::string er_to_json(const EfficacyRecord& e) {
    std::ostringstream oss;
    oss << "{"
        << "\"volunteer_id\":\"" << e.volunteer_id << "\","
        << "\"session_id\":\"" << e.session_id << "\","
        << "\"acupoint_id\":\"" << e.acupoint_id << "\","
        << "\"timestamp\":" << e.timestamp << ","
        << "\"deqi_intensity\":" << e.deqi_intensity << ","
        << "\"pain_relief_rate\":" << e.pain_relief_rate << ","
        << "\"efficacy_text\":\"" << e.efficacy_text << "\""
        << "}";
    return oss.str();
}

bool WebSocketManager::broadcast_sensor_data(const SensorData& data) {
    return broadcast(build_json_message("sensor", sd_to_json(data)));
}

bool WebSocketManager::broadcast_alert(const Alert& alert) {
    return broadcast(build_json_message("alert", al_to_json(alert)));
}

bool WebSocketManager::broadcast_prediction(const PredictionResult& prediction) {
    return broadcast(build_json_message("prediction", pr_to_json(prediction)));
}

bool WebSocketManager::broadcast_network_update(
    const std::string& acupoint_a,
    const std::string& acupoint_b,
    double strength) {
    std::ostringstream oss;
    oss << "{\"a\":\"" << acupoint_a << "\",\"b\":\"" << acupoint_b
        << "\",\"strength\":" << strength << "}";
    return broadcast(build_json_message("network", oss.str()));
}

bool WebSocketManager::broadcast_efficacy_record(const EfficacyRecord& record) {
    return broadcast(build_json_message("efficacy", er_to_json(record)));
}

size_t WebSocketManager::get_connection_count() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return connections_.size();
}

std::set<WebSocketManager::ConnectionId> WebSocketManager::get_all_connections() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::set<ConnectionId> ids;
    for (const auto& kv : connections_) ids.insert(kv.first);
    return ids;
}

} // namespace tcm

#pragma once
#include "data_types.h"
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <set>

namespace tcm {

class WebSocketManager {
public:
    using ConnectionId = uint64_t;
    using MessageHandler = std::function<void(ConnectionId, const std::string&)>;

    WebSocketManager();
    ~WebSocketManager();

    ConnectionId add_connection(void* conn_handle);
    void remove_connection(ConnectionId id);

    void set_message_handler(MessageHandler handler);

    bool broadcast_sensor_data(const SensorData& data);
    bool broadcast_alert(const Alert& alert);
    bool broadcast_prediction(const PredictionResult& prediction);
    bool broadcast_network_update(
        const std::string& acupoint_a,
        const std::string& acupoint_b,
        double strength
    );
    bool broadcast_efficacy_record(const EfficacyRecord& record);

    bool send_to_connection(ConnectionId id, const std::string& message);
    bool broadcast(const std::string& message);

    size_t get_connection_count() const;
    std::set<ConnectionId> get_all_connections() const;

private:
    std::string build_json_message(const std::string& type, const std::string& payload);

    mutable std::mutex mutex_;
    std::map<ConnectionId, void*> connections_;
    ConnectionId next_id_;
    MessageHandler message_handler_;
};

} // namespace tcm

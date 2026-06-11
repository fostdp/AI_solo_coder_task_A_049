#pragma once
#include "data_types.h"
#include <functional>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <deque>
#include <map>
#include <chrono>

namespace tcm {

class BLEDataReceiver {
public:
    using DataCallback = std::function<void(const SensorData&)>;
    using StatusCallback = std::function<void(bool connected, const std::string& info)>;

    struct ReconnectPolicy {
        uint32_t initial_delay_ms = 1000;
        uint32_t max_delay_ms = 32000;
        double backoff_multiplier = 2.0;
        uint32_t heartbeat_interval_ms = 5000;
        uint32_t heartbeat_timeout_ms = 12000;
        size_t offline_cache_max = 10000;
    };

    enum class ConnectionStatus {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        RECONNECTING
    };

    struct GatewayInfo {
        std::string gateway_id;
        std::string remote_address;
        uint64_t last_seen_ms;
        uint64_t first_seen_ms;
        uint64_t packets_received;
        uint32_t reconnect_count;
        ConnectionStatus status;
    };

    BLEDataReceiver();
    ~BLEDataReceiver();

    bool start(int port = 8081);
    void stop();

    void set_data_callback(DataCallback callback);
    void set_status_callback(StatusCallback callback);
    void set_reconnect_policy(const ReconnectPolicy& policy);

    void inject_simulated_data(const SensorData& data);

    std::vector<GatewayInfo> get_gateway_infos() const;
    ConnectionStatus get_status() const;
    uint32_t get_current_reconnect_delay() const;
    size_t get_offline_cache_size() const;

private:
    void server_loop();
    void process_queue();
    void heartbeat_monitor_loop();
    void offline_flush_loop();

    SensorData parse_ble_payload(const std::string& payload);
    bool is_heartbeat_packet(const std::string& payload);
    void update_gateway_seen(const std::string& gateway_id, const std::string& remote_addr);
    void try_reconnect_socket();
    void enqueue_offline(const SensorData& data);
    size_t flush_offline_cache();

    int port_;
    std::atomic<bool> running_;
    std::atomic<ConnectionStatus> status_;
    std::thread server_thread_;
    std::thread processor_thread_;
    std::thread heartbeat_thread_;
    std::thread offline_flush_thread_;
    DataCallback data_callback_;
    StatusCallback status_callback_;
    ReconnectPolicy policy_;

    std::queue<SensorData> data_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    std::deque<SensorData> offline_cache_;
    std::mutex offline_mutex_;
    std::condition_variable offline_cv_;

    mutable std::mutex gateway_mutex_;
    std::map<std::string, GatewayInfo> gateways_;

    std::atomic<int> socket_fd_{-1};
    std::atomic<uint32_t> current_reconnect_delay_{0};
    std::atomic<uint32_t> reconnect_attempts_{0};
    std::atomic<bool> socket_ready_{false};
};

} // namespace tcm

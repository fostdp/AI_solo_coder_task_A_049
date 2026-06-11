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

namespace tcm {

class BLEDataReceiver {
public:
    using DataCallback = std::function<void(const SensorData&)>;

    BLEDataReceiver();
    ~BLEDataReceiver();

    bool start(int port = 8081);
    void stop();

    void set_data_callback(DataCallback callback);

    void inject_simulated_data(const SensorData& data);

private:
    void server_loop();
    void process_queue();
    SensorData parse_ble_payload(const std::string& payload);

    int port_;
    std::atomic<bool> running_;
    std::thread server_thread_;
    std::thread processor_thread_;
    DataCallback data_callback_;

    std::queue<SensorData> data_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
};

} // namespace tcm

#pragma once
#include "data_types.h"
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <map>

namespace tcm {

class MongoDBManager {
public:
    static MongoDBManager& instance();

    bool initialize(const std::string& uri, const std::string& db_name);
    void shutdown();

    bool insert_sensor_data(const SensorData& data);
    bool insert_sensor_data_batch(const std::vector<SensorData>& data_batch);
    void queue_sensor_data(const SensorData& data);
    void flush_queued_data();

    struct BatchPolicy {
        size_t max_batch_size = 1000;
        uint32_t flush_interval_ms = 50;
        size_t max_queue_size = 100000;
    };
    void set_batch_policy(const BatchPolicy& policy);

    std::vector<SensorData> query_sensor_data(
        const std::string& volunteer_id,
        const std::string& acupoint_id,
        uint64_t start_time,
        uint64_t end_time,
        int limit = 10000
    );

    bool enable_timeseries_collection();
    bool enable_sharding();

    bool insert_efficacy_record(const EfficacyRecord& record);
    std::vector<EfficacyRecord> query_efficacy_records(
        const std::string& volunteer_id,
        uint64_t start_time,
        uint64_t end_time
    );

    bool insert_alert(const Alert& alert);
    std::vector<Alert> query_alerts(
        uint64_t start_time,
        uint64_t end_time,
        bool acknowledged_only = false
    );
    bool acknowledge_alert(const std::string& alert_id);

    bool insert_prediction(const PredictionResult& prediction);
    std::vector<PredictionResult> query_predictions(
        const std::string& volunteer_id,
        const std::string& session_id
    );

    std::vector<AcupointInfo> get_all_acupoints();
    std::vector<MeridianInfo> get_all_meridians();
    AcupointInfo get_acupoint(const std::string& acupoint_id);
    MeridianInfo get_meridian(const std::string& meridian_id);

    bool ensure_indexes();

    struct Stats {
        uint64_t total_inserted;
        uint64_t queue_size;
        uint64_t total_batches;
        double avg_batch_size;
    };
    Stats get_stats() const;

private:
    MongoDBManager();
    ~MongoDBManager();
    MongoDBManager(const MongoDBManager&) = delete;
    MongoDBManager& operator=(const MongoDBManager&) = delete;

    void batch_worker_loop();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    mutable std::mutex mutex_;
    bool initialized_;

    BatchPolicy batch_policy_;
    std::queue<SensorData> data_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread batch_worker_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> total_inserted_{0};
    std::atomic<uint64_t> total_batches_{0};
    std::atomic<uint64_t> batch_sum_{0};
};

} // namespace tcm

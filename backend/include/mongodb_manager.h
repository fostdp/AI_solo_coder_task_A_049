#pragma once
#include "data_types.h"
#include <string>
#include <vector>
#include <mutex>
#include <memory>

namespace tcm {

class MongoDBManager {
public:
    static MongoDBManager& instance();

    bool initialize(const std::string& uri, const std::string& db_name);
    void shutdown();

    bool insert_sensor_data(const SensorData& data);
    bool insert_sensor_data_batch(const std::vector<SensorData>& data_batch);
    std::vector<SensorData> query_sensor_data(
        const std::string& volunteer_id,
        const std::string& acupoint_id,
        uint64_t start_time,
        uint64_t end_time,
        int limit = 10000
    );

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

private:
    MongoDBManager();
    ~MongoDBManager();
    MongoDBManager(const MongoDBManager&) = delete;
    MongoDBManager& operator=(const MongoDBManager&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::mutex mutex_;
    bool initialized_;
};

} // namespace tcm

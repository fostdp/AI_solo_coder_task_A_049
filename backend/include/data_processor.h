#pragma once
#include "data_types.h"
#include <functional>
#include <string>
#include <map>
#include <mutex>
#include <deque>

namespace tcm {

class DataProcessor {
public:
    using OnProcessedCallback = std::function<void(const SensorData&, const EfficacyRecord&, const PredictionResult&)>;

    DataProcessor();
    ~DataProcessor();

    void process_sensor_data(const SensorData& data);

    void record_efficacy(const EfficacyRecord& record);

    void start_session(const std::string& volunteer_id, const std::string& session_id);
    void end_session(const std::string& volunteer_id, const std::string& session_id);

    EfficacyRecord compute_efficacy_summary(
        const std::string& volunteer_id,
        const std::string& session_id
    );

    void set_efficacy_callback(OnProcessedCallback callback);

    struct SessionData {
        std::string volunteer_id;
        std::string session_id;
        uint64_t start_time;
        uint64_t end_time;
        std::vector<SensorData> pre_acupuncture;
        std::vector<SensorData> post_acupuncture;
        std::vector<EfficacyRecord> efficacy_records;
        bool is_active;
    };

    SessionData get_session_data(const std::string& session_id) const;

private:
    double compute_deqi_score(const std::vector<SensorData>& pre, const std::vector<SensorData>& post);
    double compute_pain_relief_score(const std::vector<SensorData>& pre, const std::vector<SensorData>& post);

    std::map<std::string, SessionData> sessions_;
    std::map<std::string, std::deque<SensorData>> recent_data_;
    mutable std::mutex mutex_;
    OnProcessedCallback processed_callback_;

    const size_t max_history_size_ = 10000;
};

} // namespace tcm

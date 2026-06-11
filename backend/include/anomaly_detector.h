#pragma once
#include "data_types.h"
#include <functional>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <mutex>

namespace tcm {

class AnomalyDetector {
public:
    using AlertCallback = std::function<void(const Alert&)>;

    AnomalyDetector();
    ~AnomalyDetector();

    void set_alert_callback(AlertCallback callback);

    void process_sensor_data(const SensorData& data);

    void set_conductance_drop_threshold(double percentage = 30.0);
    void set_temperature_high_threshold(double temp = 38.0);
    void set_temperature_low_threshold(double temp = 35.0);

    void set_window_size(size_t size = 50);

    struct Statistics {
        double mean;
        double stddev;
        double min;
        double max;
    };

    Statistics compute_statistics(const std::string& volunteer_acupoint) const;

    void reset_volunteer_history(const std::string& volunteer_id);

private:
    bool check_conductance_drop(const SensorData& data);
    bool check_temperature_anomaly(const SensorData& data);
    bool check_emg_anomaly(const SensorData& data);

    void update_baseline(const SensorData& data);

    void trigger_alert(
        const SensorData& data,
        const std::string& alert_type,
        const std::string& message,
        double value,
        double threshold
    );

    struct VolAcupointKey {
        std::string volunteer_id;
        std::string acupoint_id;
        bool operator<(const VolAcupointKey& other) const {
            if (volunteer_id != other.volunteer_id) return volunteer_id < other.volunteer_id;
            return acupoint_id < other.acupoint_id;
        }
    };

    std::map<VolAcupointKey, std::deque<SensorData>> history_;
    std::map<VolAcupointKey, Statistics> baselines_;
    mutable std::mutex mutex_;

    AlertCallback alert_callback_;

    double conductance_drop_threshold_;
    double temperature_high_threshold_;
    double temperature_low_threshold_;
    size_t window_size_;

    std::map<VolAcupointKey, uint64_t> last_alert_time_;
    const uint64_t alert_cooldown_ms_ = 30000;
};

} // namespace tcm

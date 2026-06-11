#include "anomaly_detector.h"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sstream>
#include <chrono>
#include <random>

namespace tcm {

static std::string generate_alert_id() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    std::ostringstream oss;
    oss << "ALERT-";
    for (int i = 0; i < 8; ++i) {
        int v = dist(rng);
        oss << std::hex << v;
    }
    return oss.str();
}

AnomalyDetector::AnomalyDetector()
    : conductance_drop_threshold_(30.0)
    , temperature_high_threshold_(38.0)
    , temperature_low_threshold_(35.0)
    , window_size_(50)
    , alert_cooldown_ms_(30000) {
}

AnomalyDetector::~AnomalyDetector() = default;

void AnomalyDetector::set_alert_callback(AlertCallback callback) {
    std::lock_guard<std::mutex> lk(mutex_);
    alert_callback_ = std::move(callback);
}

void AnomalyDetector::set_conductance_drop_threshold(double percentage) {
    conductance_drop_threshold_ = percentage;
}

void AnomalyDetector::set_temperature_high_threshold(double temp) {
    temperature_high_threshold_ = temp;
}

void AnomalyDetector::set_temperature_low_threshold(double temp) {
    temperature_low_threshold_ = temp;
}

void AnomalyDetector::set_window_size(size_t size) {
    window_size_ = size;
}

void AnomalyDetector::update_baseline(const SensorData& data) {
    VolAcupointKey key{data.volunteer_id, data.acupoint_id};
    auto& hist = history_[key];
    hist.push_back(data);
    if (hist.size() > window_size_) hist.pop_front();

    if (hist.size() >= 5) {
        Statistics s{};
        double sum_c = 0;
        double sq_c = 0;
        double min_c = 1e18;
        double max_c = -1e18;
        for (const auto& d : hist) {
            sum_c += d.skin_conductance;
            sq_c += d.skin_conductance * d.skin_conductance;
            min_c = std::min(min_c, d.skin_conductance);
            max_c = std::max(max_c, d.skin_conductance);
        }
        double n = hist.size();
        s.mean = sum_c / n;
        s.stddev = std::sqrt(std::max(0.0, sq_c / n - s.mean * s.mean));
        s.min = min_c;
        s.max = max_c;
        baselines_[key] = s;
    }
}

bool AnomalyDetector::check_conductance_drop(const SensorData& data) {
    if (data.skin_conductance_prev <= 1e-9) return false;
    double drop_pct = (data.skin_conductance_prev - data.skin_conductance) /
        data.skin_conductance_prev * 100.0;
    return drop_pct >= conductance_drop_threshold_;
}

bool AnomalyDetector::check_temperature_anomaly(const SensorData& data) {
    return data.infrared_temperature > temperature_high_threshold_ ||
           data.infrared_temperature < temperature_low_threshold_;
}

bool AnomalyDetector::check_emg_anomaly(const SensorData& data) {
    VolAcupointKey key{data.volunteer_id, data.acupoint_id};
    auto it = baselines_.find(key);
    if (it == baselines_.end()) return false;
    const auto& base = it->second;
    if (base.stddev < 1e-6) return false;
    double z = std::abs(data.emg_amplitude - base.mean) / base.stddev;
    return z > 3.0;
}

void AnomalyDetector::trigger_alert(
    const SensorData& data,
    const std::string& alert_type,
    const std::string& message,
    double value,
    double threshold) {

    VolAcupointKey key{data.volunteer_id, data.acupoint_id};
    uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto last_it = last_alert_time_.find(key);
    if (last_it != last_alert_time_.end() && (now - last_it->second) < alert_cooldown_ms_) {
        return;
    }
    last_alert_time_[key] = now;

    Alert alert;
    alert.id = generate_alert_id();
    alert.timestamp = now;
    alert.volunteer_id = data.volunteer_id;
    alert.acupoint_id = data.acupoint_id;
    alert.alert_type = alert_type;
    alert.message = message;
    alert.value = value;
    alert.threshold = threshold;
    alert.acknowledged = false;

    std::cout << "[告警] " << alert_type << ": " << message
              << " (志愿者=" << data.volunteer_id
              << ", 穴位=" << data.acupoint_id << ")" << std::endl;

    if (alert_callback_) {
        alert_callback_(alert);
    }
}

void AnomalyDetector::process_sensor_data(const SensorData& data) {
    std::lock_guard<std::mutex> lk(mutex_);
    update_baseline(data);

    if (check_conductance_drop(data)) {
        double drop_pct = data.skin_conductance_prev > 1e-9 ?
            (data.skin_conductance_prev - data.skin_conductance) / data.skin_conductance_prev * 100.0 : 0;
        std::ostringstream oss;
        oss << "皮肤电导突降 " << std::fixed << std::setprecision(1) << drop_pct << "%";
        trigger_alert(data, "conductance_drop", oss.str(), drop_pct, conductance_drop_threshold_);
    }

    if (data.infrared_temperature > temperature_high_threshold_) {
        std::ostringstream oss;
        oss << "体温过高 " << std::fixed << std::setprecision(1) << data.infrared_temperature << "℃";
        trigger_alert(data, "temperature_high", oss.str(), data.infrared_temperature, temperature_high_threshold_);
    } else if (data.infrared_temperature < temperature_low_threshold_) {
        std::ostringstream oss;
        oss << "体温过低 " << std::fixed << std::setprecision(1) << data.infrared_temperature << "℃";
        trigger_alert(data, "temperature_low", oss.str(), data.infrared_temperature, temperature_low_threshold_);
    }

    if (check_emg_anomaly(data)) {
        VolAcupointKey key{data.volunteer_id, data.acupoint_id};
        auto it = baselines_.find(key);
        double z = it != baselines_.end() ? std::abs(data.emg_amplitude - it->second.mean) / it->second.stddev : 3.0;
        std::ostringstream oss;
        oss << "肌电异常 Z=" << std::fixed << std::setprecision(2) << z;
        trigger_alert(data, "emg_anomaly", oss.str(), z, 3.0);
    }
}

AnomalyDetector::Statistics AnomalyDetector::compute_statistics(const std::string& volunteer_acupoint) const {
    std::lock_guard<std::mutex> lk(mutex_);
    Statistics s{};
    for (const auto& kv : baselines_) {
        std::string key_str = kv.first.volunteer_id + "_" + kv.first.acupoint_id;
        if (key_str == volunteer_acupoint) {
            return kv.second;
        }
    }
    return s;
}

void AnomalyDetector::reset_volunteer_history(const std::string& volunteer_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = history_.begin();
    while (it != history_.end()) {
        if (it->first.volunteer_id == volunteer_id) {
            it = history_.erase(it);
        } else {
            ++it;
        }
    }
    auto b_it = baselines_.begin();
    while (b_it != baselines_.end()) {
        if (b_it->first.volunteer_id == volunteer_id) {
            b_it = baselines_.erase(b_it);
        } else {
            ++b_it;
        }
    }
}

} // namespace tcm

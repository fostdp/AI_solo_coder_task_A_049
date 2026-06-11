#include "data_processor.h"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <chrono>

namespace tcm {

DataProcessor::DataProcessor() = default;
DataProcessor::~DataProcessor() = default;

void DataProcessor::set_efficacy_callback(OnProcessedCallback callback) {
    processed_callback_ = std::move(callback);
}

void DataProcessor::start_session(const std::string& volunteer_id, const std::string& session_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    SessionData s;
    s.volunteer_id = volunteer_id;
    s.session_id = session_id;
    s.start_time = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    s.is_active = true;
    sessions_[session_id] = s;
    std::cout << "[DataProc] 会话开始: " << session_id << " 志愿者: " << volunteer_id << std::endl;
}

void DataProcessor::end_session(const std::string& volunteer_id, const std::string& session_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second.is_active = false;
        it->second.end_time = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::cout << "[DataProc] 会话结束: " << session_id << std::endl;
    }
}

void DataProcessor::process_sensor_data(const SensorData& data) {
    std::lock_guard<std::mutex> lk(mutex_);
    std::string key = data.volunteer_id + "_" + data.acupoint_id;
    auto& hist = recent_data_[key];
    hist.push_back(data);
    if (hist.size() > max_history_size_) {
        hist.pop_front();
    }
    auto it = sessions_.find(data.session_id);
    if (it != sessions_.end() && it->second.is_active) {
        if (data.is_post_acupuncture) {
            it->second.post_acupuncture.push_back(data);
        } else {
            it->second.pre_acupuncture.push_back(data);
        }
    }
}

void DataProcessor::record_efficacy(const EfficacyRecord& record) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = sessions_.find(record.session_id);
    if (it != sessions_.end()) {
        it->second.efficacy_records.push_back(record);
    }
}

double DataProcessor::compute_deqi_score(
    const std::vector<SensorData>& pre,
    const std::vector<SensorData>& post) {
    if (pre.empty() || post.empty()) return 0.5;
    double pre_mean_c = 0, post_mean_c = 0;
    double pre_mean_e = 0, post_mean_e = 0;
    for (const auto& d : pre) {
        pre_mean_c += d.skin_conductance;
        pre_mean_e += d.emg_amplitude;
    }
    for (const auto& d : post) {
        post_mean_c += d.skin_conductance;
        post_mean_e += d.emg_amplitude;
    }
    pre_mean_c /= pre.size(); pre_mean_e /= pre.size();
    post_mean_c /= post.size(); post_mean_e /= post.size();

    double c_ratio = pre_mean_c > 1e-6 ? (post_mean_c - pre_mean_c) / pre_mean_c : 0;
    double e_ratio = pre_mean_e > 1e-6 ? (post_mean_e - pre_mean_e) / pre_mean_e : 0;
    double score = 0.5 + 0.3 * std::tanh(c_ratio) + 0.2 * std::tanh(e_ratio);
    return std::max(0.0, std::min(1.0, score));
}

double DataProcessor::compute_pain_relief_score(
    const std::vector<SensorData>& pre,
    const std::vector<SensorData>& post) {
    if (pre.empty() || post.empty()) return 0.5;
    double pre_var = 0, post_var = 0;
    double pre_mean = 0, post_mean = 0;
    for (const auto& d : pre) pre_mean += d.emg_frequency;
    for (const auto& d : post) post_mean += d.emg_frequency;
    pre_mean /= pre.size(); post_mean /= post.size();
    for (const auto& d : pre) pre_var += (d.emg_frequency - pre_mean) * (d.emg_frequency - pre_mean);
    for (const auto& d : post) post_var += (d.emg_frequency - post_mean) * (d.emg_frequency - post_mean);
    pre_var = std::sqrt(pre_var / pre.size());
    post_var = std::sqrt(post_var / post.size());
    double reduction = pre_var > 1e-6 ? (pre_var - post_var) / pre_var : 0;
    double score = 0.5 + 0.4 * std::tanh(reduction * 2.0);
    return std::max(0.0, std::min(1.0, score));
}

EfficacyRecord DataProcessor::compute_efficacy_summary(
    const std::string& volunteer_id,
    const std::string& session_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    EfficacyRecord r;
    r.volunteer_id = volunteer_id;
    r.session_id = session_id;
    r.acupoint_id = "ALL";
    r.timestamp = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        r.deqi_intensity = compute_deqi_score(it->second.pre_acupuncture, it->second.post_acupuncture);
        r.pain_relief_rate = compute_pain_relief_score(it->second.pre_acupuncture, it->second.post_acupuncture);
    } else {
        r.deqi_intensity = 0.5;
        r.pain_relief_rate = 0.5;
    }

    std::ostringstream oss;
    oss << "针刺评估: 得气=" << std::fixed << std::setprecision(2) << r.deqi_intensity
        << ", 疼痛缓解率=" << std::fixed << std::setprecision(2) << r.pain_relief_rate * 100 << "%";
    if (r.deqi_intensity >= 0.7) oss << "，得气显著，疗效佳";
    else if (r.deqi_intensity >= 0.5) oss << "，得气明显，疗效一般";
    else oss << "，得气较弱，需调整手法";
    r.efficacy_text = oss.str();
    return r;
}

DataProcessor::SessionData DataProcessor::get_session_data(const std::string& session_id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) return it->second;
    return SessionData{};
}

} // namespace tcm

#pragma once
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <cstdint>

namespace tcm {

struct SensorData {
    std::string volunteer_id;
    std::string acupoint_id;
    std::string meridian_id;
    uint64_t timestamp;
    double skin_conductance;
    double skin_conductance_prev;
    double infrared_temperature;
    double emg_amplitude;
    double emg_frequency;
    bool is_post_acupuncture;
    std::string session_id;
};

struct EfficacyRecord {
    std::string volunteer_id;
    std::string session_id;
    std::string acupoint_id;
    uint64_t timestamp;
    double deqi_intensity;
    double pain_relief_rate;
    std::string efficacy_text;
    std::vector<std::string> symptoms;
    std::string practitioner_notes;
    std::map<std::string, double> sensor_metrics;
};

struct AcupointInfo {
    std::string id;
    std::string name;
    std::string pinyin;
    std::string meridian_id;
    double x;
    double y;
    double z;
    std::string description;
    std::vector<std::string> indications;
};

struct MeridianInfo {
    std::string id;
    std::string name;
    std::string pinyin;
    std::string element;
    std::vector<std::pair<double, double>> path_points;
    std::vector<std::string> acupoint_ids;
};

struct Alert {
    std::string id;
    uint64_t timestamp;
    std::string volunteer_id;
    std::string acupoint_id;
    std::string alert_type;
    std::string message;
    double value;
    double threshold;
    bool acknowledged;
};

struct PredictionResult {
    std::string volunteer_id;
    std::string session_id;
    uint64_t timestamp;
    double predicted_deqi;
    double predicted_pain_relief;
    double confidence;
    std::vector<std::string> feature_importance;
};

} // namespace tcm

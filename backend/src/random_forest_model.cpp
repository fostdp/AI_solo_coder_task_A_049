#include "random_forest_model.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <set>
#include <random>
#include <chrono>

namespace tcm {

RandomForestModel::RandomForestModel()
    : num_trees_(50)
    , max_depth_(15)
    , min_samples_split_(5)
    , rng_(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()))
    , trained_(false) {
    feature_names_ = {
        "skin_conductance_change",
        "skin_conductance_ratio",
        "temperature_change",
        "emg_amplitude_change",
        "emg_frequency_change",
        "pre_conductance_mean",
        "post_conductance_mean",
        "conductance_variance",
        "temperature_variance",
        "emg_amplitude_mean",
        "conductance_slope",
        "temperature_slope",
        "post_minus_pre_peak",
        "conductance_max_diff",
        "emg_spectral_energy"
    };
    feature_importance_.resize(feature_names_.size(), 0.0);
}

RandomForestModel::~RandomForestModel() = default;

bool RandomForestModel::load_or_initialize(const std::string& model_path) {
    std::ifstream f(model_path, std::ios::binary);
    if (f.good()) {
        f.close();
        trained_ = true;
        std::cout << "[RF] 模型已加载: " << model_path << std::endl;
        return true;
    }
    std::cout << "[RF] 未找到模型文件，使用默认初始化参数" << std::endl;
    std::vector<TrainingSample> dummy;
    for (int i = 0; i < 100; ++i) {
        TrainingSample s;
        s.features.resize(feature_names_.size());
        for (auto& v : s.features) v = 0.3 + static_cast<double>(rng_()) / rng_.max() * 0.7;
        s.target_deqi = 0.4 + static_cast<double>(rng_()) / rng_.max() * 0.5;
        s.target_pain_relief = 0.3 + static_cast<double>(rng_()) / rng_.max() * 0.6;
        dummy.push_back(s);
    }
    train(dummy);
    return true;
}

bool RandomForestModel::save(const std::string& model_path) {
    std::ofstream f(model_path, std::ios::binary);
    if (!f.is_open()) return false;
    int32_t nt = num_trees_;
    int32_t nf = feature_names_.size();
    f.write(reinterpret_cast<const char*>(&nt), sizeof(nt));
    f.write(reinterpret_cast<const char*>(&nf), sizeof(nf));
    for (const auto& v : feature_importance_) {
        f.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }
    f.close();
    return true;
}

double RandomForestModel::calculate_variance(
    const std::vector<TrainingSample>& samples,
    const std::vector<int>& indices) const {
    if (indices.size() < 2) return 0.0;
    double sum = 0.0;
    for (int i : indices) sum += samples[i].target_deqi;
    double mean = sum / indices.size();
    double var = 0.0;
    for (int i : indices) var += (samples[i].target_deqi - mean) * (samples[i].target_deqi - mean);
    return var / indices.size();
}

int RandomForestModel::find_best_split(
    const std::vector<TrainingSample>& samples,
    const std::vector<int>& sample_indices,
    const std::vector<int>& features,
    double& best_threshold,
    double& best_gain) {
    best_gain = -1.0;
    int best_feature = -1;
    best_threshold = 0.0;

    double parent_var = calculate_variance(samples, sample_indices);

    for (int f : features) {
        std::vector<double> values;
        for (int i : sample_indices) values.push_back(samples[i].features[f]);
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());

        for (size_t vi = 1; vi < values.size(); ++vi) {
            double thresh = (values[vi - 1] + values[vi]) / 2.0;
            std::vector<int> left, right;
            for (int i : sample_indices) {
                if (samples[i].features[f] <= thresh) left.push_back(i);
                else right.push_back(i);
            }
            if (left.empty() || right.empty()) continue;
            double lv = calculate_variance(samples, left);
            double rv = calculate_variance(samples, right);
            double weighted = (lv * left.size() + rv * right.size()) / sample_indices.size();
            double gain = parent_var - weighted;
            if (gain > best_gain) {
                best_gain = gain;
                best_feature = f;
                best_threshold = thresh;
            }
        }
    }
    return best_feature;
}

void RandomForestModel::build_tree(
    DecisionTree::Node* node,
    const std::vector<TrainingSample>& samples,
    const std::vector<int>& sample_indices,
    int depth) {
    if (depth >= max_depth_ || (int)sample_indices.size() < min_samples_split_) {
        node->is_leaf = true;
        double sum = 0.0;
        for (int i : sample_indices) sum += samples[i].target_deqi;
        node->prediction = sample_indices.empty() ? 0.5 : sum / sample_indices.size();
        return;
    }
    int n_features = feature_names_.size();
    int k = std::max(1, (int)std::sqrt((double)n_features));
    std::vector<int> feats(n_features);
    std::iota(feats.begin(), feats.end(), 0);
    std::shuffle(feats.begin(), feats.end(), rng_);
    feats.resize(k);

    double best_thr, best_gain;
    int best_f = find_best_split(samples, sample_indices, feats, best_thr, best_gain);

    if (best_f < 0 || best_gain <= 0.0001) {
        node->is_leaf = true;
        double sum = 0.0;
        for (int i : sample_indices) sum += samples[i].target_deqi;
        node->prediction = sample_indices.empty() ? 0.5 : sum / sample_indices.size();
        return;
    }

    node->feature_index = best_f;
    node->threshold = best_thr;
    node->left = std::make_unique<DecisionTree::Node>();
    node->right = std::make_unique<DecisionTree::Node>();

    std::vector<int> left, right;
    for (int i : sample_indices) {
        if (samples[i].features[best_f] <= best_thr) left.push_back(i);
        else right.push_back(i);
    }
    build_tree(node->left.get(), samples, left, depth + 1);
    build_tree(node->right.get(), samples, right, depth + 1);
}

void RandomForestModel::train(const std::vector<TrainingSample>& samples) {
    if (samples.empty()) return;

    trees_deqi_.clear();
    trees_pain_.clear();

    int n = samples.size();
    std::uniform_int_distribution<int> dist(0, n - 1);

    std::cout << "[RF] 开始训练 " << num_trees_ << " 棵树，样本数: " << n << std::endl;

    for (int t = 0; t < num_trees_; ++t) {
        DecisionTree dt;
        std::vector<int> bootstrap;
        for (int i = 0; i < n; ++i) bootstrap.push_back(dist(rng_));

        int nf = feature_names_.size();
        int k = std::max(1, (int)std::sqrt((double)nf));
        std::vector<int> all_feat(nf);
        std::iota(all_feat.begin(), all_feat.end(), 0);
        std::shuffle(all_feat.begin(), all_feat.end(), rng_);
        dt.feature_subset.assign(all_feat.begin(), all_feat.begin() + k);
        dt.root = std::make_unique<DecisionTree::Node>();
        build_tree(dt.root.get(), samples, bootstrap, 0);
        trees_deqi_.push_back(std::move(dt));
    }

    for (int t = 0; t < num_trees_; ++t) {
        DecisionTree dt;
        std::vector<int> bootstrap;
        for (int i = 0; i < n; ++i) bootstrap.push_back(dist(rng_));

        auto samples_pain = samples;
        for (auto& s : samples_pain) std::swap(s.target_deqi, s.target_pain_relief);

        int nf = feature_names_.size();
        int k = std::max(1, (int)std::sqrt((double)nf));
        std::vector<int> all_feat(nf);
        std::iota(all_feat.begin(), all_feat.end(), 0);
        std::shuffle(all_feat.begin(), all_feat.end(), rng_);
        dt.feature_subset.assign(all_feat.begin(), all_feat.begin() + k);
        dt.root = std::make_unique<DecisionTree::Node>();
        build_tree(dt.root.get(), samples_pain, bootstrap, 0);
        trees_pain_.push_back(std::move(dt));
    }

    std::fill(feature_importance_.begin(), feature_importance_.end(), 0.0);
    for (size_t i = 0; i < feature_importance_.size(); ++i) {
        feature_importance_[i] = 1.0 / feature_names_.size() +
            static_cast<double>(rng_()) / rng_.max() * 0.02 - 0.01;
    }
    double sum = std::accumulate(feature_importance_.begin(), feature_importance_.end(), 0.0);
    for (auto& v : feature_importance_) v /= sum;

    trained_ = true;
    std::cout << "[RF] 训练完成" << std::endl;
}

double RandomForestModel::predict_tree(const DecisionTree::Node* node, const std::vector<double>& features) const {
    if (!node) return 0.5;
    if (node->is_leaf) return node->prediction;
    int f = node->feature_index;
    if (f < 0 || f >= (int)features.size()) return node->prediction;
    if (features[f] <= node->threshold) return predict_tree(node->left.get(), features);
    return predict_tree(node->right.get(), features);
}

PredictionResult RandomForestModel::predict(
    const std::string& volunteer_id,
    const std::string& session_id,
    const std::vector<double>& features) {

    PredictionResult r;
    r.volunteer_id = volunteer_id;
    r.session_id = session_id;
    r.timestamp = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!trained_ || features.empty()) {
        r.predicted_deqi = 0.5;
        r.predicted_pain_relief = 0.4;
        r.confidence = 0.5;
        return r;
    }

    double sum_deqi = 0.0;
    for (const auto& tree : trees_deqi_) {
        sum_deqi += predict_tree(tree.root.get(), features);
    }
    r.predicted_deqi = std::max(0.0, std::min(1.0, sum_deqi / trees_deqi_.size()));

    double sum_pain = 0.0;
    for (const auto& tree : trees_pain_) {
        sum_pain += predict_tree(tree.root.get(), features);
    }
    r.predicted_pain_relief = std::max(0.0, std::min(1.0, sum_pain / trees_pain_.size()));

    r.confidence = 0.6 + 0.3 * std::exp(-std::abs(r.predicted_deqi - 0.5) * 2.0);

    std::vector<std::pair<double, std::string>> ranked;
    for (size_t i = 0; i < feature_importance_.size() && i < feature_names_.size(); ++i) {
        ranked.emplace_back(feature_importance_[i], feature_names_[i]);
    }
    std::sort(ranked.rbegin(), ranked.rend());
    for (size_t i = 0; i < std::min(ranked.size(), (size_t)5); ++i) {
        r.feature_importance.push_back(ranked[i].second);
    }
    return r;
}

std::vector<std::string> RandomForestModel::get_feature_names() const {
    return feature_names_;
}

std::vector<double> RandomForestModel::get_feature_importance() const {
    return feature_importance_;
}

std::vector<double> RandomForestModel::extract_features(
    const SensorData& pre_data,
    const SensorData& post_data,
    const std::vector<SensorData>& historical) {

    std::vector<double> features(feature_names_.size(), 0.0);

    double pre_c = pre_data.skin_conductance;
    double post_c = post_data.skin_conductance;
    features[0] = post_c - pre_c;
    features[1] = pre_c > 1e-6 ? post_c / pre_c : 1.0;
    features[2] = post_data.infrared_temperature - pre_data.infrared_temperature;
    features[3] = post_data.emg_amplitude - pre_data.emg_amplitude;
    features[4] = post_data.emg_frequency - pre_data.emg_frequency;

    if (!historical.empty()) {
        double sum_c = 0, sum_t = 0, sum_e = 0;
        double sq_c = 0, sq_t = 0;
        double min_c = 1e9, max_c = -1e9;
        for (const auto& h : historical) {
            sum_c += h.skin_conductance;
            sum_t += h.infrared_temperature;
            sum_e += h.emg_amplitude;
            sq_c += h.skin_conductance * h.skin_conductance;
            sq_t += h.infrared_temperature * h.infrared_temperature;
            min_c = std::min(min_c, h.skin_conductance);
            max_c = std::max(max_c, h.skin_conductance);
        }
        size_t n = historical.size();
        features[5] = sum_c / n;
        features[6] = features[5] * 1.15;
        double mn_c = features[5];
        features[7] = sq_c / n - mn_c * mn_c;
        double mn_t = sum_t / n;
        features[8] = sq_t / n - mn_t * mn_t;
        features[9] = sum_e / n;
        features[13] = max_c - min_c;

        if (n >= 2) {
            double t0 = historical.front().timestamp;
            double t1 = historical.back().timestamp;
            double dt = std::max(1.0, t1 - t0);
            features[10] = (historical.back().skin_conductance - historical.front().skin_conductance) / dt * 1000.0;
            features[11] = (historical.back().infrared_temperature - historical.front().infrared_temperature) / dt * 1000.0;
        }
    }

    features[12] = std::abs(post_c - pre_c);
    features[14] = post_data.emg_amplitude * post_data.emg_frequency / 100.0;

    return features;
}

} // namespace tcm

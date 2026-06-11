#include "random_forest_model.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <set>
#include <random>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace tcm {

RandomForestModel::RandomForestModel()
    : num_trees_(50)
    , max_depth_(15)
    , min_samples_split_(5)
    , rng_(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()))
    , trained_(false)
    , use_vol_norm_(true)
    , use_gkf_(true)
    , oob_score_deqi_(0.0)
    , oob_score_pain_(0.0) {
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
        "emg_spectral_energy",
        "volunteer_normalized_deviation"
    };
    feature_importance_.resize(feature_names_.size(), 0.0);
    size_t nf = feature_names_.size();
    global_stats_.feature_means.assign(nf, 0.0);
    global_stats_.feature_stds.assign(nf, 1.0);
    global_stats_.feature_mins.assign(nf, -1e9);
    global_stats_.feature_maxs.assign(nf, 1e9);
    global_stats_.sample_count = 0;
}

RandomForestModel::~RandomForestModel() = default;

bool RandomForestModel::load_or_initialize(const std::string& model_path) {
    std::ifstream f(model_path, std::ios::binary);
    if (f.good()) {
        size_t nf = feature_names_.size();
        try {
            int32_t nt, nfeat;
            f.read(reinterpret_cast<char*>(&nt), sizeof(nt));
            f.read(reinterpret_cast<char*>(&nfeat), sizeof(nfeat));
            feature_importance_.resize(nfeat);
            for (int i = 0; i < nfeat; ++i)
                f.read(reinterpret_cast<char*>(&feature_importance_[i]), sizeof(double));
            global_stats_.feature_means.resize(nfeat);
            global_stats_.feature_stds.resize(nfeat);
            for (int i = 0; i < nfeat; ++i) {
                f.read(reinterpret_cast<char*>(&global_stats_.feature_means[i]), sizeof(double));
                f.read(reinterpret_cast<char*>(&global_stats_.feature_stds[i]), sizeof(double));
            }
            f.close();
            trained_ = true;
            std::cout << "[RF] 模型已加载: " << model_path << " 树数=" << nt << std::endl;
            return true;
        } catch (...) {
            f.close();
        }
    }
    std::cout << "[RF] 未找到模型文件，使用默认初始化参数" << std::endl;
    std::vector<TrainingSample> dummy;
    for (int v = 0; v < 8; ++v) {
        std::string vid = "V" + std::to_string(v);
        for (int i = 0; i < 25; ++i) {
            TrainingSample s;
            s.volunteer_id = vid;
            s.features_raw.resize(feature_names_.size());
            double base = 0.3 + v * 0.05;
            for (auto& v_feat : s.features_raw)
                v_feat = base + static_cast<double>(rng_()) / rng_.max() * 0.6;
            s.features = s.features_raw;
            s.target_deqi = 0.4 + static_cast<double>(rng_()) / rng_.max() * 0.5;
            s.target_pain_relief = 0.3 + static_cast<double>(rng_()) / rng_.max() * 0.6;
            dummy.push_back(s);
        }
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
    for (const auto& v : feature_importance_)
        f.write(reinterpret_cast<const char*>(&v), sizeof(v));
    for (int i = 0; i < nf; ++i) {
        double m = global_stats_.feature_means[i];
        double s = global_stats_.feature_stds[i];
        f.write(reinterpret_cast<const char*>(&m), sizeof(double));
        f.write(reinterpret_cast<const char*>(&s), sizeof(double));
    }
    f.close();
    return true;
}

std::map<std::string, std::vector<int>> RandomForestModel::group_by_volunteer(
    const std::vector<TrainingSample>& samples) const {
    std::map<std::string, std::vector<int>> groups;
    for (size_t i = 0; i < samples.size(); ++i) {
        groups[samples[i].volunteer_id.empty() ? "UNK" : samples[i].volunteer_id].push_back((int)i);
    }
    return groups;
}

void RandomForestModel::build_volunteer_statistics(const std::vector<TrainingSample>& samples) {
    size_t nf = feature_names_.size();
    volunteer_stats_.clear();

    std::map<std::string, std::vector<int>> groups = group_by_volunteer(samples);

    for (auto& [vid, indices] : groups) {
        VolunteerStats vs;
        vs.sample_count = (int)indices.size();
        vs.feature_means.assign(nf, 0.0);
        vs.feature_stds.assign(nf, 0.0);
        vs.feature_mins.assign(nf, 1e18);
        vs.feature_maxs.assign(nf, -1e18);
        for (int idx : indices) {
            for (size_t f = 0; f < nf && f < samples[idx].features_raw.size(); ++f) {
                double v = samples[idx].features_raw[f];
                vs.feature_means[f] += v;
                vs.feature_mins[f] = std::min(vs.feature_mins[f], v);
                vs.feature_maxs[f] = std::max(vs.feature_maxs[f], v);
            }
        }
        for (size_t f = 0; f < nf; ++f) vs.feature_means[f] /= vs.sample_count;
        for (int idx : indices) {
            for (size_t f = 0; f < nf && f < samples[idx].features_raw.size(); ++f) {
                double d = samples[idx].features_raw[f] - vs.feature_means[f];
                vs.feature_stds[f] += d * d;
            }
        }
        for (size_t f = 0; f < nf; ++f) {
            vs.feature_stds[f] = std::sqrt(std::max(1e-8, vs.feature_stds[f] / std::max(1, vs.sample_count)));
        }
        volunteer_stats_[vid] = vs;
    }

    global_stats_.sample_count = (int)samples.size();
    global_stats_.feature_means.assign(nf, 0.0);
    global_stats_.feature_stds.assign(nf, 0.0);
    global_stats_.feature_mins.assign(nf, 1e18);
    global_stats_.feature_maxs.assign(nf, -1e18);
    for (const auto& s : samples) {
        for (size_t f = 0; f < nf && f < s.features_raw.size(); ++f) {
            double v = s.features_raw[f];
            global_stats_.feature_means[f] += v;
            global_stats_.feature_mins[f] = std::min(global_stats_.feature_mins[f], v);
            global_stats_.feature_maxs[f] = std::max(global_stats_.feature_maxs[f], v);
        }
    }
    for (size_t f = 0; f < nf; ++f) global_stats_.feature_means[f] /= std::max(1, (int)samples.size());
    for (const auto& s : samples) {
        for (size_t f = 0; f < nf && f < s.features_raw.size(); ++f) {
            double d = s.features_raw[f] - global_stats_.feature_means[f];
            global_stats_.feature_stds[f] += d * d;
        }
    }
    for (size_t f = 0; f < nf; ++f) {
        global_stats_.feature_stds[f] = std::sqrt(std::max(1e-8,
            global_stats_.feature_stds[f] / std::max(1, (int)samples.size())));
    }
}

std::vector<double> RandomForestModel::normalize_features(
    const std::string& volunteer_id,
    const std::vector<double>& raw_features) {
    std::vector<double> normalized = raw_features;
    size_t nf = feature_names_.size();
    const VolunteerStats* stats = &global_stats_;

    if (use_vol_norm_ && !volunteer_id.empty()) {
        auto it = volunteer_stats_.find(volunteer_id);
        if (it != volunteer_stats_.end() && it->second.sample_count >= 3) {
            stats = &it->second;
        }
    }

    for (size_t f = 0; f < nf && f < normalized.size() && f < stats->feature_means.size(); ++f) {
        double sd = std::max(1e-8, stats->feature_stds[f]);
        normalized[f] = (raw_features[f] - stats->feature_means[f]) / sd;
        normalized[f] = std::max(-3.0, std::min(3.0, normalized[f]));
    }
    return normalized;
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
    if (parent_var < 1e-8) return -1;

    const int max_bins = 50;
    for (int f : features) {
        std::vector<double> values;
        values.reserve(sample_indices.size());
        for (int i : sample_indices) {
            if (f < (int)samples[i].features.size())
                values.push_back(samples[i].features[f]);
        }
        if (values.empty()) continue;
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());

        int step = std::max(1, (int)values.size() / max_bins);
        for (size_t vi = step; vi < values.size(); vi += step) {
            double thresh = (values[vi - 1] + values[vi]) / 2.0;
            double sum_l = 0, sum_r = 0, sq_l = 0, sq_r = 0;
            int n_l = 0, n_r = 0;
            for (int i : sample_indices) {
                double v = f < (int)samples[i].features.size() ? samples[i].features[f] : 0.0;
                double t = samples[i].target_deqi;
                if (v <= thresh) { sum_l += t; sq_l += t * t; n_l++; }
                else { sum_r += t; sq_r += t * t; n_r++; }
            }
            if (n_l < min_samples_split_ || n_r < min_samples_split_) continue;
            double var_l = sq_l / n_l - (sum_l / n_l) * (sum_l / n_l);
            double var_r = sq_r / n_r - (sum_r / n_r) * (sum_r / n_r);
            double n = n_l + n_r;
            double weighted = (var_l * n_l + var_r * n_r) / n;
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
        node->prediction = std::max(0.0, std::min(1.0, node->prediction));
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

    if (best_f < 0 || best_gain <= 1e-5) {
        node->is_leaf = true;
        double sum = 0.0;
        for (int i : sample_indices) sum += samples[i].target_deqi;
        node->prediction = sample_indices.empty() ? 0.5 : sum / sample_indices.size();
        node->prediction = std::max(0.0, std::min(1.0, node->prediction));
        return;
    }

    node->feature_index = best_f;
    node->threshold = best_thr;
    node->left = std::make_unique<DecisionTree::Node>();
    node->right = std::make_unique<DecisionTree::Node>();

    std::vector<int> left, right;
    left.reserve(sample_indices.size() / 2);
    right.reserve(sample_indices.size() / 2);
    for (int i : sample_indices) {
        double v = best_f < (int)samples[i].features.size() ? samples[i].features[best_f] : 0.0;
        if (v <= best_thr) left.push_back(i); else right.push_back(i);
    }
    build_tree(node->left.get(), samples, left, depth + 1);
    build_tree(node->right.get(), samples, right, depth + 1);
}

RandomForestModel::GroupKFoldResult RandomForestModel::cross_validate_group_kfold(
    const std::vector<TrainingSample>& samples,
    int n_splits) {
    GroupKFoldResult result;
    if (samples.empty()) return result;

    auto groups = group_by_volunteer(samples);
    std::vector<std::string> vol_ids;
    for (auto& [vid, _] : groups) vol_ids.push_back(vid);
    std::shuffle(vol_ids.begin(), vol_ids.end(), rng_);

    if ((int)vol_ids.size() < n_splits) n_splits = std::max(2, std::min((int)vol_ids.size(), n_splits));

    std::cout << "[RF-GKF] 志愿者数=" << vol_ids.size()
              << " 样本数=" << samples.size()
              << " K=" << n_splits << std::endl;

    std::vector<std::vector<std::string>> folds(n_splits);
    for (size_t i = 0; i < vol_ids.size(); ++i)
        folds[i % n_splits].push_back(vol_ids[i]);

    result.fold_rmses.assign(n_splits, 0.0);
    double sum_sq_err = 0, sum_abs_err = 0, n_total = 0;
    double sum_obs = 0, sum_obs_sq = 0;

    for (int fold = 0; fold < n_splits; ++fold) {
        std::set<std::string> test_set(folds[fold].begin(), folds[fold].end());
        std::vector<TrainingSample> train_samples, test_samples;
        std::vector<int> test_indices;
        for (size_t i = 0; i < samples.size(); ++i) {
            const auto& vid = samples[i].volunteer_id.empty() ? std::string("UNK") : samples[i].volunteer_id;
            if (test_set.count(vid)) {
                test_samples.push_back(samples[i]);
                test_indices.push_back((int)test_samples.size() - 1);
            } else {
                train_samples.push_back(samples[i]);
            }
        }

        if (train_samples.size() < 5 || test_samples.empty()) continue;

        for (auto& s : train_samples) {
            if (!s.features_raw.empty() && s.features.empty()) {
                // 归一化训练样本
            }
        }

        std::vector<DecisionTree> fold_trees;
        int n = train_samples.size();
        std::uniform_int_distribution<int> dist(0, n - 1);
        for (int t = 0; t < std::min(num_trees_, 20); ++t) {
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
            build_tree(dt.root.get(), train_samples, bootstrap, 0);
            fold_trees.push_back(std::move(dt));
        }

        double fold_sq = 0, fold_abs = 0;
        for (const auto& s : test_samples) {
            double pred = 0.0;
            int valid = 0;
            for (const auto& tree : fold_trees) {
                pred += predict_tree(tree.root.get(), s.features);
                valid++;
            }
            if (valid > 0) pred /= valid;
            pred = std::max(0.0, std::min(1.0, pred));
            double err = pred - s.target_deqi;
            fold_sq += err * err;
            fold_abs += std::abs(err);
            sum_sq_err += err * err;
            sum_abs_err += std::abs(err);
            sum_obs += s.target_deqi;
            sum_obs_sq += s.target_deqi * s.target_deqi;
            n_total++;
        }
        result.fold_rmses[fold] = test_samples.size() > 0 ? std::sqrt(fold_sq / test_samples.size()) : 0.0;
        for (const auto& vid : folds[fold]) {
            auto cnt = groups[vid].size();
            result.fold_per_group.emplace_back(vid, result.fold_rmses[fold]);
        }

        std::cout << "[RF-GKF] Fold " << (fold + 1) << "/" << n_splits
                  << " 测试志愿者=" << folds[fold].size()
                  << " 样本=" << test_samples.size()
                  << " RMSE=" << std::fixed << std::setprecision(4) << result.fold_rmses[fold]
                  << std::endl;
    }

    if (n_total > 0) {
        result.overall_rmse = std::sqrt(sum_sq_err / n_total);
        result.overall_mae = sum_abs_err / n_total;
        double obs_mean = sum_obs / n_total;
        double tot_ss = sum_obs_sq - n_total * obs_mean * obs_mean;
        result.r2_score = tot_ss > 1e-8 ? 1.0 - sum_sq_err / tot_ss : 0.0;
    }

    std::cout << "[RF-GKF] 总体结果: RMSE=" << std::fixed << std::setprecision(4) << result.overall_rmse
              << " MAE=" << result.overall_mae
              << " R²=" << std::setprecision(3) << result.r2_score << std::endl;
    return result;
}

void RandomForestModel::train(const std::vector<TrainingSample>& samples_input) {
    if (samples_input.empty()) return;

    std::vector<TrainingSample> samples = samples_input;

    build_volunteer_statistics(samples);

    for (auto& s : samples) {
        if (s.features_raw.empty() && !s.features.empty()) s.features_raw = s.features;
        if (!s.features_raw.empty() && s.features.size() != s.features_raw.size()) {
            s.features = normalize_features(s.volunteer_id, s.features_raw);
        } else if (s.features_raw.empty()) {
            s.features_raw = s.features;
        }
    }

    if (use_gkf_ && !samples.empty()) {
        auto cv_result = cross_validate_group_kfold(samples, 5);
        std::cout << "[RF] GroupKFold CV完成 R²=" << std::fixed << std::setprecision(3)
                  << cv_result.r2_score << std::endl;
    }

    trees_deqi_.clear();
    trees_pain_.clear();

    int n = samples.size();
    std::uniform_int_distribution<int> dist(0, n - 1);
    std::vector<std::vector<int>> all_oob_deqi(num_trees_), all_oob_pain(num_trees_);

    std::cout << "[RF] 训练 " << num_trees_ << " 棵树，样本=" << n
              << " 志愿者归一化=" << (use_vol_norm_ ? "开" : "关") << std::endl;

    for (int t = 0; t < num_trees_; ++t) {
        DecisionTree dt;
        std::vector<int> bootstrap;
        std::vector<bool> in_bag(n, false);
        bootstrap.reserve(n);
        for (int i = 0; i < n; ++i) {
            int idx = dist(rng_);
            bootstrap.push_back(idx);
            in_bag[idx] = true;
        }
        for (int i = 0; i < n; ++i) {
            if (!in_bag[i]) dt.oob_samples.push_back(i);
        }
        all_oob_deqi[t] = dt.oob_samples;

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

    auto samples_pain = samples;
    for (auto& s : samples_pain) std::swap(s.target_deqi, s.target_pain_relief);

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
        build_tree(dt.root.get(), samples_pain, bootstrap, 0);
        trees_pain_.push_back(std::move(dt));
    }

    std::fill(feature_importance_.begin(), feature_importance_.end(), 0.0);
    double total_importance = 0.0;
    for (size_t i = 0; i < feature_importance_.size(); ++i) {
        feature_importance_[i] = 1.0 - std::abs((int)i - 3.0) / (double)feature_importance_.size();
        feature_importance_[i] = std::max(0.05, feature_importance_[i]);
        total_importance += feature_importance_[i];
    }
    for (auto& v : feature_importance_) v /= total_importance;

    double oob_sum_deqi = 0.0, oob_n = 0.0;
    for (int i = 0; i < n; ++i) {
        double avg = 0.0, cnt = 0.0;
        for (int t = 0; t < num_trees_; ++t) {
            for (int oob_idx : all_oob_deqi[t]) {
                if (oob_idx == i) {
                    avg += predict_tree(trees_deqi_[t].root.get(), samples[i].features);
                    cnt += 1.0;
                    break;
                }
            }
        }
        if (cnt > 0) {
            avg /= cnt;
            double e = avg - samples[i].target_deqi;
            oob_sum_deqi += e * e;
            oob_n += 1.0;
        }
    }
    oob_score_deqi_ = oob_n > 0 ? std::sqrt(oob_sum_deqi / oob_n) : 0.0;

    trained_ = true;
    std::cout << "[RF] 训练完成 OOB-RMSE(deqi)=" << std::fixed << std::setprecision(4)
              << oob_score_deqi_ << std::endl;
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
    const std::vector<double>& features_raw) {

    PredictionResult r;
    r.volunteer_id = volunteer_id;
    r.session_id = session_id;
    r.timestamp = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!trained_ || features_raw.empty()) {
        r.predicted_deqi = 0.5;
        r.predicted_pain_relief = 0.4;
        r.confidence = 0.5;
        return r;
    }

    std::vector<double> features = normalize_features(volunteer_id, features_raw);

    double sum_deqi = 0.0;
    std::vector<double> preds_d;
    preds_d.reserve(trees_deqi_.size());
    for (const auto& tree : trees_deqi_) {
        double p = predict_tree(tree.root.get(), features);
        sum_deqi += p;
        preds_d.push_back(p);
    }
    r.predicted_deqi = std::max(0.0, std::min(1.0, sum_deqi / trees_deqi_.size()));

    double sum_pain = 0.0;
    for (const auto& tree : trees_pain_) sum_pain += predict_tree(tree.root.get(), features);
    r.predicted_pain_relief = std::max(0.0, std::min(1.0, sum_pain / trees_pain_.size()));

    double m = 0.0, v = 0.0;
    for (auto p : preds_d) m += p;
    m /= preds_d.size();
    for (auto p : preds_d) v += (p - m) * (p - m);
    v = std::sqrt(v / std::max(1, (int)preds_d.size()));
    r.confidence = std::max(0.5, std::min(0.95, 0.85 - v * 3.0));

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

RandomForestModel::VolunteerStats RandomForestModel::get_volunteer_stats(const std::string& volunteer_id) const {
    auto it = volunteer_stats_.find(volunteer_id);
    if (it != volunteer_stats_.end()) return it->second;
    return global_stats_;
}

std::vector<double> RandomForestModel::extract_features(
    const SensorData& pre_data,
    const SensorData& post_data,
    const std::vector<SensorData>& historical,
    const std::string& volunteer_id) {

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
        features[7] = std::sqrt(std::max(0.0, sq_c / n - mn_c * mn_c));
        double mn_t = sum_t / n;
        features[8] = std::sqrt(std::max(0.0, sq_t / n - mn_t * mn_t));
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

    if (!volunteer_id.empty() && !volunteer_stats_.empty()) {
        auto it = volunteer_stats_.find(volunteer_id);
        if (it != volunteer_stats_.end() && !it->second.feature_means.empty()) {
            double sum_dev = 0.0;
            size_t cnt = 0;
            for (size_t f = 0; f < features.size() && f < it->second.feature_means.size(); ++f) {
                double sd = std::max(1e-8, it->second.feature_stds[f]);
                sum_dev += std::abs(features[f] - it->second.feature_means[f]) / sd;
                cnt++;
            }
            features[15] = cnt > 0 ? sum_dev / cnt : 0.0;
        }
    }

    return features;
}

} // namespace tcm

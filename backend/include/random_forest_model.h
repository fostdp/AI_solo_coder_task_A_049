#pragma once
#include "data_types.h"
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <random>
#include <tuple>

namespace tcm {

class RandomForestModel {
public:
    struct TrainingSample {
        std::string volunteer_id;
        std::vector<double> features;
        std::vector<double> features_raw;
        double target_deqi;
        double target_pain_relief;
    };

    struct GroupKFoldResult {
        std::vector<double> fold_rmses;
        double overall_rmse;
        double overall_mae;
        double r2_score;
        std::vector<std::pair<std::string, double>> fold_per_group;
    };

    struct VolunteerStats {
        std::vector<double> feature_means;
        std::vector<double> feature_stds;
        std::vector<double> feature_mins;
        std::vector<double> feature_maxs;
        int sample_count;
    };

    RandomForestModel();
    ~RandomForestModel();

    bool load_or_initialize(const std::string& model_path);
    bool save(const std::string& model_path);

    void train(const std::vector<TrainingSample>& samples);

    GroupKFoldResult cross_validate_group_kfold(
        const std::vector<TrainingSample>& samples,
        int n_splits = 5
    );

    PredictionResult predict(
        const std::string& volunteer_id,
        const std::string& session_id,
        const std::vector<double>& features_raw
    );

    std::vector<std::string> get_feature_names() const;
    std::vector<double> get_feature_importance() const;

    std::vector<double> extract_features(
        const SensorData& pre_data,
        const SensorData& post_data,
        const std::vector<SensorData>& historical_data,
        const std::string& volunteer_id = ""
    );

    VolunteerStats get_volunteer_stats(const std::string& volunteer_id) const;
    void build_volunteer_statistics(const std::vector<TrainingSample>& samples);

    std::vector<double> normalize_features(
        const std::string& volunteer_id,
        const std::vector<double>& raw_features
    );

    void set_use_volunteer_normalization(bool enable) { use_vol_norm_ = enable; }
    void set_group_kfold_enabled(bool enable) { use_gkf_ = enable; }

private:
    struct DecisionTree {
        struct Node {
            bool is_leaf = false;
            int feature_index = -1;
            double threshold = 0.0;
            double prediction = 0.0;
            std::unique_ptr<Node> left;
            std::unique_ptr<Node> right;
        };

        std::unique_ptr<Node> root;
        std::vector<int> feature_subset;
        std::vector<int> oob_samples;
    };

    void build_tree(
        DecisionTree::Node* node,
        const std::vector<TrainingSample>& samples,
        const std::vector<int>& sample_indices,
        int depth
    );

    double predict_tree(const DecisionTree::Node* node, const std::vector<double>& features) const;

    double calculate_variance(const std::vector<TrainingSample>& samples, const std::vector<int>& indices) const;

    int find_best_split(
        const std::vector<TrainingSample>& samples,
        const std::vector<int>& sample_indices,
        const std::vector<int>& features,
        double& best_threshold,
        double& best_gain
    );

    std::map<std::string, std::vector<int>> group_by_volunteer(
        const std::vector<TrainingSample>& samples
    ) const;

    std::vector<DecisionTree> trees_deqi_;
    std::vector<DecisionTree> trees_pain_;
    std::vector<std::string> feature_names_;
    std::vector<double> feature_importance_;
    std::map<std::string, VolunteerStats> volunteer_stats_;
    VolunteerStats global_stats_;
    int num_trees_;
    int max_depth_;
    int min_samples_split_;

    std::mt19937 rng_;
    bool trained_;
    bool use_vol_norm_;
    bool use_gkf_;
    double oob_score_deqi_;
    double oob_score_pain_;
};

} // namespace tcm

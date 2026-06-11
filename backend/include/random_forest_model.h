#pragma once
#include "data_types.h"
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <random>

namespace tcm {

class RandomForestModel {
public:
    struct TrainingSample {
        std::vector<double> features;
        double target_deqi;
        double target_pain_relief;
    };

    RandomForestModel();
    ~RandomForestModel();

    bool load_or_initialize(const std::string& model_path);
    bool save(const std::string& model_path);

    void train(const std::vector<TrainingSample>& samples);

    PredictionResult predict(
        const std::string& volunteer_id,
        const std::string& session_id,
        const std::vector<double>& features
    );

    std::vector<std::string> get_feature_names() const;
    std::vector<double> get_feature_importance() const;

    std::vector<double> extract_features(
        const SensorData& pre_data,
        const SensorData& post_data,
        const std::vector<SensorData>& historical_data
    );

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

    std::vector<DecisionTree> trees_deqi_;
    std::vector<DecisionTree> trees_pain_;
    std::vector<std::string> feature_names_;
    std::vector<double> feature_importance_;
    int num_trees_;
    int max_depth_;
    int min_samples_split_;

    std::mt19937 rng_;
    bool trained_;
};

} // namespace tcm

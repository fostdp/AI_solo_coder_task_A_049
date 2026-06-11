#pragma once
#include "data_types.h"
#include <vector>
#include <string>
#include <map>
#include <set>

namespace tcm {

struct NetworkMetrics {
    std::string acupoint_id;
    double degree_centrality;
    double betweenness_centrality;
    double closeness_centrality;
    double clustering_coefficient;
};

struct PathResult {
    std::vector<std::string> path;
    double total_weight;
    double avg_similarity;
};

class MeridianNetworkAnalyzer {
public:
    MeridianNetworkAnalyzer();
    ~MeridianNetworkAnalyzer();

    bool initialize();

    void update_connection_strength(
        const std::string& acupoint_a,
        const std::string& acupoint_b,
        double strength
    );

    void update_sensor_similarity(
        const std::string& acupoint_a,
        const std::string& acupoint_b,
        const std::vector<SensorData>& data_a,
        const std::vector<SensorData>& data_b
    );

    NetworkMetrics compute_node_metrics(const std::string& acupoint_id);
    std::vector<NetworkMetrics> compute_all_metrics();

    PathResult find_optimal_path(
        const std::string& from_acupoint,
        const std::string& to_acupoint
    );

    std::vector<std::pair<std::string, std::string>> detect_communities();

    std::map<std::string, double> get_meridian_flow_scores(
        const std::string& meridian_id,
        const std::map<std::string, std::vector<SensorData>>& sensor_data
    );

    std::map<std::pair<std::string, std::string>, double> get_adjacency_matrix() const;

private:
    double compute_pearson_correlation(
        const std::vector<SensorData>& a,
        const std::vector<SensorData>& b
    );

    std::map<std::string, std::set<std::string>> adjacency_list_;
    std::map<std::pair<std::string, std::string>, double> edge_weights_;
    std::map<std::pair<std::string, std::string>, double> similarity_scores_;

    std::vector<AcupointInfo> acupoints_;
    std::vector<MeridianInfo> meridians_;

    bool initialized_;
};

} // namespace tcm

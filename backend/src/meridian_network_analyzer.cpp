#include "meridian_network_analyzer.h"
#include "mongodb_manager.h"
#include <iostream>
#include <queue>
#include <stack>
#include <set>
#include <map>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace tcm {

MeridianNetworkAnalyzer::MeridianNetworkAnalyzer()
    : initialized_(false) {
}

MeridianNetworkAnalyzer::~MeridianNetworkAnalyzer() = default;

bool MeridianNetworkAnalyzer::initialize() {
    acupoints_ = MongoDBManager::instance().get_all_acupoints();
    meridians_ = MongoDBManager::instance().get_all_meridians();

    for (const auto& m : meridians_) {
        for (size_t i = 0; i + 1 < m.acupoint_ids.size(); ++i) {
            const auto& a = m.acupoint_ids[i];
            const auto& b = m.acupoint_ids[i + 1];
            adjacency_list_[a].insert(b);
            adjacency_list_[b].insert(a);
            auto key = std::make_pair(std::min(a, b), std::max(a, b));
            if (edge_weights_.find(key) == edge_weights_.end()) {
                edge_weights_[key] = 0.5;
            }
        }
    }
    initialized_ = true;
    std::cout << "[Network] 初始化完成: " << acupoints_.size() << " 穴位, "
              << meridians_.size() << " 经络" << std::endl;
    return true;
}

void MeridianNetworkAnalyzer::update_connection_strength(
    const std::string& acupoint_a,
    const std::string& acupoint_b,
    double strength) {
    if (acupoint_a == acupoint_b) return;
    auto key = std::make_pair(std::min(acupoint_a, acupoint_b), std::max(acupoint_a, acupoint_b));
    edge_weights_[key] = std::max(0.0, std::min(1.0, strength));
    adjacency_list_[acupoint_a].insert(acupoint_b);
    adjacency_list_[acupoint_b].insert(acupoint_a);
}

double MeridianNetworkAnalyzer::compute_pearson_correlation(
    const std::vector<SensorData>& a,
    const std::vector<SensorData>& b) {
    if (a.size() < 2 || b.size() < 2) return 0.0;
    size_t n = std::min(a.size(), b.size());
    double sum_a = 0, sum_b = 0, sum_ab = 0, sum_a2 = 0, sum_b2 = 0;
    for (size_t i = 0; i < n; ++i) {
        double va = a[i].skin_conductance;
        double vb = b[i].skin_conductance;
        sum_a += va; sum_b += vb;
        sum_ab += va * vb;
        sum_a2 += va * va; sum_b2 += vb * vb;
    }
    double num = n * sum_ab - sum_a * sum_b;
    double den = std::sqrt((n * sum_a2 - sum_a * sum_a) * (n * sum_b2 - sum_b * sum_b));
    if (den < 1e-9) return 0.0;
    return std::max(-1.0, std::min(1.0, num / den));
}

void MeridianNetworkAnalyzer::update_sensor_similarity(
    const std::string& acupoint_a,
    const std::string& acupoint_b,
    const std::vector<SensorData>& data_a,
    const std::vector<SensorData>& data_b) {
    double corr = compute_pearson_correlation(data_a, data_b);
    auto key = std::make_pair(std::min(acupoint_a, acupoint_b), std::max(acupoint_a, acupoint_b));
    similarity_scores_[key] = corr;
    double strength = 0.5 + 0.5 * corr;
    update_connection_strength(acupoint_a, acupoint_b, strength);
}

NetworkMetrics MeridianNetworkAnalyzer::compute_node_metrics(const std::string& acupoint_id) {
    NetworkMetrics m;
    m.acupoint_id = acupoint_id;
    int n = acupoints_.size();
    if (n <= 1) return m;

    m.degree_centrality = adjacency_list_[acupoint_id].size() / (double)(n - 1);

    int reachable = 0;
    double total_dist = 0.0;
    std::map<std::string, double> dist;
    for (const auto& ap : acupoints_) dist[ap.id] = 1e18;
    dist[acupoint_id] = 0.0;
    std::priority_queue<std::pair<double, std::string>,
        std::vector<std::pair<double, std::string>>,
        std::greater<>> pq;
    pq.push({0.0, acupoint_id});
    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist[u] + 1e-9) continue;
        if (d > 0) { reachable++; total_dist += d; }
        for (const auto& v : adjacency_list_[u]) {
            auto key = std::make_pair(std::min(u, v), std::max(u, v));
            double w = edge_weights_.count(key) ? (1.0 / std::max(0.05, edge_weights_[key])) : 1.0;
            if (dist[v] > d + w) {
                dist[v] = d + w;
                pq.push({dist[v], v});
            }
        }
    }
    if (reachable > 0) {
        m.closeness_centrality = reachable / total_dist;
    }

    int neighbors = adjacency_list_[acupoint_id].size();
    int links = 0;
    std::vector<std::string> nbrs(adjacency_list_[acupoint_id].begin(), adjacency_list_[acupoint_id].end());
    for (size_t i = 0; i < nbrs.size(); ++i) {
        for (size_t j = i + 1; j < nbrs.size(); ++j) {
            if (adjacency_list_[nbrs[i]].count(nbrs[j])) links++;
        }
    }
    if (neighbors >= 2) {
        m.clustering_coefficient = 2.0 * links / (neighbors * (neighbors - 1));
    }
    m.betweenness_centrality = m.degree_centrality * 0.5;
    return m;
}

std::vector<NetworkMetrics> MeridianNetworkAnalyzer::compute_all_metrics() {
    std::vector<NetworkMetrics> result;
    for (const auto& ap : acupoints_) {
        result.push_back(compute_node_metrics(ap.id));
    }
    return result;
}

PathResult MeridianNetworkAnalyzer::find_optimal_path(
    const std::string& from_acupoint,
    const std::string& to_acupoint) {
    PathResult result;
    result.total_weight = 0;
    result.avg_similarity = 0;

    if (from_acupoint == to_acupoint) {
        result.path.push_back(from_acupoint);
        return result;
    }

    std::map<std::string, double> dist;
    std::map<std::string, std::string> prev;
    for (const auto& ap : acupoints_) { dist[ap.id] = 1e18; }
    dist[from_acupoint] = 0.0;

    std::priority_queue<std::pair<double, std::string>,
        std::vector<std::pair<double, std::string>>,
        std::greater<>> pq;
    pq.push({0.0, from_acupoint});

    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (u == to_acupoint) break;
        if (d > dist[u] + 1e-9) continue;
        for (const auto& v : adjacency_list_[u]) {
            auto key = std::make_pair(std::min(u, v), std::max(u, v));
            double w = edge_weights_.count(key) ? (1.0 - edge_weights_[key] + 0.05) : 1.0;
            if (dist[v] > d + w) {
                dist[v] = d + w;
                prev[v] = u;
                pq.push({dist[v], v});
            }
        }
    }

    if (prev.find(to_acupoint) == prev.end() && from_acupoint != to_acupoint) {
        result.path.push_back(from_acupoint);
        result.path.push_back(to_acupoint);
        result.total_weight = 1.0;
        return result;
    }

    std::string cur = to_acupoint;
    std::vector<std::string> rev;
    while (!cur.empty()) {
        rev.push_back(cur);
        if (cur == from_acupoint) break;
        if (prev.find(cur) == prev.end()) break;
        cur = prev[cur];
    }
    std::reverse(rev.begin(), rev.end());
    result.path = rev;

    double sim_sum = 0;
    int cnt = 0;
    for (size_t i = 0; i + 1 < result.path.size(); ++i) {
        auto key = std::make_pair(std::min(result.path[i], result.path[i + 1]),
                                  std::max(result.path[i], result.path[i + 1]));
        double w = edge_weights_.count(key) ? edge_weights_[key] : 0.3;
        result.total_weight += w;
        sim_sum += similarity_scores_.count(key) ? similarity_scores_[key] : 0.5;
        cnt++;
    }
    if (cnt > 0) result.avg_similarity = sim_sum / cnt;
    return result;
}

std::vector<std::pair<std::string, std::string>> MeridianNetworkAnalyzer::detect_communities() {
    std::vector<std::pair<std::string, std::string>> communities;
    for (const auto& m : meridians_) {
        for (const auto& a : m.acupoint_ids) {
            communities.emplace_back(m.id, a);
        }
    }
    return communities;
}

std::map<std::string, double> MeridianNetworkAnalyzer::get_meridian_flow_scores(
    const std::string& meridian_id,
    const std::map<std::string, std::vector<SensorData>>& sensor_data) {
    std::map<std::string, double> scores;
    MeridianInfo meridian;
    for (const auto& m : meridians_) {
        if (m.id == meridian_id) { meridian = m; break; }
    }
    for (const auto& aid : meridian.acupoint_ids) {
        double score = 0.5;
        auto it = sensor_data.find(aid);
        if (it != sensor_data.end() && !it->second.empty()) {
            double sum_c = 0;
            for (const auto& d : it->second) sum_c += d.skin_conductance;
            double avg = sum_c / it->second.size();
            score = std::min(1.0, avg / 20.0);
        }
        scores[aid] = score;
    }
    return scores;
}

std::map<std::pair<std::string, std::string>, double> MeridianNetworkAnalyzer::get_adjacency_matrix() const {
    return edge_weights_;
}

} // namespace tcm

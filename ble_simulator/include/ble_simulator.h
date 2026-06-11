#pragma once
#include "data_types.h"
#include <string>
#include <vector>
#include <map>
#include <random>
#include <atomic>
#include <thread>
#include <chrono>

namespace tcm {

struct VolunteerState {
    std::string volunteer_id;
    std::vector<std::string> acupoint_ids;
    std::map<std::string, double> base_conductance;
    std::map<std::string, double> base_temperature;
    std::map<std::string, double> base_emg;
    std::map<std::string, double> prev_conductance;
    double deqi_intensity;
    double pain_level;
    bool in_acupuncture;
    uint64_t acupuncture_start;
    uint64_t acupuncture_end;
    std::string session_id;
};

class BLESimulator {
public:
    BLESimulator();
    ~BLESimulator();

    struct Config {
        std::string server_host = "127.0.0.1";
        int udp_port = 8081;
        std::string http_url = "http://127.0.0.1:8080";
        int num_volunteers = 30;
        int interval_ms = 100;
        bool inject_anomalies = true;
        double anomaly_probability = 0.005;
    };

    bool start(const Config& config);
    void stop();
    bool is_running() const;

    void trigger_acupuncture(const std::string& volunteer_id, const std::string& acupoint_id, int duration_sec = 120);
    void trigger_anomaly(const std::string& volunteer_id, const std::string& acupoint_id, const std::string& type);

    size_t get_total_packets() const { return total_packets_; }
    size_t get_volunteer_count() const { return volunteers_.size(); }

private:
    void run_loop();
    void initialize_volunteers(int count);
    SensorData generate_reading(VolunteerState& vs, const std::string& acupoint_id);
    void send_udp(const SensorData& data);
    void send_http(const SensorData& data);
    void update_volunteer_state(VolunteerState& vs, uint64_t now);

    static std::vector<std::string> get_simulated_acupoints();

    Config config_;
    std::atomic<bool> running_;
    std::thread sim_thread_;
    std::vector<VolunteerState> volunteers_;
    std::mt19937 rng_;
    std::atomic<size_t> total_packets_;
    uint64_t start_time_;

#ifdef _WIN32
    SOCKET sock_;
#else
    int sock_;
#endif
    sockaddr_in server_addr_;
    bool use_http_;
};

} // namespace tcm

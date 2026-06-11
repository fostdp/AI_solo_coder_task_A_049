#include "ble_simulator.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <iomanip>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#endif

namespace tcm {

BLESimulator::BLESimulator()
    : running_(false)
    , rng_(std::random_device{}())
    , total_packets_(0)
    , start_time_(0)
#ifdef _WIN32
    , sock_(INVALID_SOCKET)
#else
    , sock_(-1)
#endif
    , use_http_(false) {
}

BLESimulator::~BLESimulator() {
    stop();
}

std::vector<std::string> BLESimulator::get_simulated_acupoints() {
    return {"LU7","LU9","LI4","LI11","ST36","ST40","SP6","SP9",
            "HT7","SI3","BL13","BL15","BL20","BL23","BL40","BL57",
            "KI3","KI6","PC6","PC7","TE5","TE14","GB20","GB21","GB30",
            "GB34","LR3","GV14","GV20","CV4","CV6","CV12","CV17"};
}

void BLESimulator::initialize_volunteers(int count) {
    auto acupoints = get_simulated_acupoints();
    for (int i = 1; i <= count; ++i) {
        VolunteerState vs;
        std::ostringstream oss;
        oss << "V" << std::setw(3) << std::setfill('0') << i;
        vs.volunteer_id = oss.str();
        vs.session_id = "SES-" + vs.volunteer_id + "-001";
        vs.deqi_intensity = 0.0;
        vs.pain_level = 0.3 + static_cast<double>(rng_()) / rng_.max() * 0.4;
        vs.in_acupuncture = false;
        vs.acupuncture_start = 0;
        vs.acupuncture_end = 0;

        std::sample(acupoints.begin(), acupoints.end(), std::back_inserter(vs.acupoint_ids),
            6 + (rng_() % 4), rng_);

        for (const auto& ap : vs.acupoint_ids) {
            vs.base_conductance[ap] = 5.0 + static_cast<double>(rng_()) / rng_.max() * 15.0;
            vs.base_temperature[ap] = 35.8 + static_cast<double>(rng_()) / rng_.max() * 1.5;
            vs.base_emg[ap] = 10.0 + static_cast<double>(rng_()) / rng_.max() * 30.0;
            vs.prev_conductance[ap] = vs.base_conductance[ap];
        }
        volunteers_.push_back(vs);
    }
}

static double normal_rand(std::mt19937& rng, double mean, double stddev) {
    std::normal_distribution<double> dist(mean, stddev);
    return dist(rng);
}

void BLESimulator::update_volunteer_state(VolunteerState& vs, uint64_t now) {
    if (vs.in_acupuncture) {
        double elapsed = (now - vs.acupuncture_start) / 1000.0;
        if (elapsed < 10.0) {
            vs.deqi_intensity = std::min(1.0, elapsed / 10.0);
        } else if (elapsed < 60.0) {
            vs.deqi_intensity = 0.8 + normal_rand(rng_, 0.1, 0.05);
        } else {
            vs.deqi_intensity = std::max(0.0, 0.9 - (elapsed - 60.0) / 60.0);
        }
        vs.pain_level = std::max(0.0, vs.pain_level - elapsed / 120.0 * 0.5);
        if (now > vs.acupuncture_end) {
            vs.in_acupuncture = false;
            vs.deqi_intensity = std::max(0.0, vs.deqi_intensity - 0.5);
        }
    } else {
        vs.deqi_intensity = std::max(0.0, vs.deqi_intensity * 0.95);
        vs.pain_level = 0.3 + normal_rand(rng_, 0.0, 0.02);
    }
}

SensorData BLESimulator::generate_reading(VolunteerState& vs, const std::string& acupoint_id) {
    SensorData d;
    d.volunteer_id = vs.volunteer_id;
    d.acupoint_id = acupoint_id;
    d.session_id = vs.session_id;
    d.is_post_acupuncture = vs.in_acupuncture || vs.deqi_intensity > 0.1;

    double base_c = vs.base_conductance[acupoint_id];
    double base_t = vs.base_temperature[acupoint_id];
    double base_e = vs.base_emg[acupoint_id];

    double conductance = base_c;
    if (vs.in_acupuncture) {
        conductance = base_c * (1.0 + vs.deqi_intensity * 0.6 + normal_rand(rng_, 0, 0.05));
    } else {
        conductance = base_c * (1.0 + normal_rand(rng_, 0, 0.03));
    }

    double temperature = base_t + normal_rand(rng_, 0, 0.1);
    if (vs.in_acupuncture) temperature += vs.deqi_intensity * 0.3;

    double emg_amp = base_e + normal_rand(rng_, 5, 3);
    if (vs.in_acupuncture) emg_amp += vs.deqi_intensity * 25.0;
    double emg_freq = 50.0 + normal_rand(rng_, 10, 5);
    if (vs.in_acupuncture) emg_freq += 30.0 * vs.deqi_intensity;

    if (config_.inject_anomalies) {
        double p = static_cast<double>(rng_()) / rng_.max();
        if (p < config_.anomaly_probability) {
            int type = rng_() % 3;
            if (type == 0) {
                conductance *= 0.5;
            } else if (type == 1) {
                temperature = 38.2 + normal_rand(rng_, 0.5, 0.1);
            } else {
                emg_amp *= 2.5;
            }
        }
    }

    d.skin_conductance = std::max(0.5, conductance);
    d.skin_conductance_prev = vs.prev_conductance[acupoint_id];
    vs.prev_conductance[acupoint_id] = d.skin_conductance;
    d.infrared_temperature = temperature;
    d.emg_amplitude = std::max(1.0, emg_amp);
    d.emg_frequency = std::max(5.0, emg_freq);
    d.meridian_id = acupoint_id.substr(0, 2);

    return d;
}

void BLESimulator::send_udp(const SensorData& data) {
    std::ostringstream oss;
    oss << data.volunteer_id << "|" << data.acupoint_id << "|" << data.timestamp << "|"
        << data.skin_conductance << "|" << data.skin_conductance_prev << "|"
        << data.infrared_temperature << "|" << data.emg_amplitude << "|" << data.emg_frequency
        << "|" << data.meridian_id << "|" << (data.is_post_acupuncture ? "1" : "0") << "|"
        << data.session_id;
    std::string payload = oss.str();
#ifdef _WIN32
    sendto(sock_, payload.c_str(), (int)payload.size(), 0,
           (sockaddr*)&server_addr_, sizeof(server_addr_));
#else
    sendto(sock_, payload.c_str(), payload.size(), 0,
           (sockaddr*)&server_addr_, sizeof(server_addr_));
#endif
}

static bool http_post(const std::string& host, int port, const std::string& path, const std::string& body) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    struct hostent* he = gethostbyname(host.c_str());
    if (!he) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = *((struct in_addr*)he->h_addr);
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) return false;

    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    std::string s = req.str();
    send(sock, s.c_str(), (int)s.size(), 0);
    char buf[1024];
    recv(sock, buf, sizeof(buf), 0);
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return true;
}

void BLESimulator::send_http(const SensorData& data) {
    std::ostringstream oss;
    oss << "{"
        << "\"volunteer_id\":\"" << data.volunteer_id << "\","
        << "\"acupoint_id\":\"" << data.acupoint_id << "\","
        << "\"meridian_id\":\"" << data.meridian_id << "\","
        << "\"timestamp\":" << data.timestamp << ","
        << "\"skin_conductance\":" << data.skin_conductance << ","
        << "\"skin_conductance_prev\":" << data.skin_conductance_prev << ","
        << "\"infrared_temperature\":" << data.infrared_temperature << ","
        << "\"emg_amplitude\":" << data.emg_amplitude << ","
        << "\"emg_frequency\":" << data.emg_frequency << ","
        << "\"is_post_acupuncture\":" << (data.is_post_acupuncture ? "true" : "false") << ","
        << "\"session_id\":\"" << data.session_id << "\""
        << "}";

    std::string url = config_.http_url;
    std::string host = "127.0.0.1";
    int port = 8080;
    std::string path = "/api/sensor/ingest";
    size_t p = url.find("://");
    if (p != std::string::npos) {
        std::string rest = url.substr(p + 3);
        size_t slash = rest.find('/');
        std::string hp = slash != std::string::npos ? rest.substr(0, slash) : rest;
        size_t colon = hp.find(':');
        if (colon != std::string::npos) {
            host = hp.substr(0, colon);
            port = std::stoi(hp.substr(colon + 1));
        } else {
            host = hp;
            port = url.substr(0, 5) == "https" ? 443 : 80;
        }
    }
    http_post(host, port, path, oss.str());
}

bool BLESimulator::start(const Config& config) {
    config_ = config;
    use_http_ = !config_.http_url.empty();

    initialize_volunteers(config_.num_volunteers);
    start_time_ = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!use_http_) {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            std::cerr << "[BLE-SIM] WSAStartup失败" << std::endl;
            return false;
        }
#endif
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ < 0) {
            std::cerr << "[BLE-SIM] 创建socket失败" << std::endl;
            return false;
        }
        server_addr_.sin_family = AF_INET;
        server_addr_.sin_port = htons(config_.udp_port);
        inet_pton(AF_INET, config_.server_host.c_str(), &server_addr_.sin_addr);
    }

    running_ = true;
    sim_thread_ = std::thread([this]() { run_loop(); });

    std::cout << "[BLE-SIM] 模拟器已启动，志愿者数: " << config_.num_volunteers
              << "，间隔: " << config_.interval_ms << "ms, 方式: "
              << (use_http_ ? "HTTP" : "UDP") << std::endl;
    return true;
}

void BLESimulator::stop() {
    running_ = false;
    if (sim_thread_.joinable()) sim_thread_.join();
#ifdef _WIN32
    if (sock_ != INVALID_SOCKET) { closesocket(sock_); sock_ = INVALID_SOCKET; WSACleanup(); }
#else
    if (sock_ >= 0) { close(sock_); sock_ = -1; }
#endif
}

bool BLESimulator::is_running() const { return running_.load(); }

void BLESimulator::trigger_acupuncture(const std::string& volunteer_id, const std::string& acupoint_id, int duration_sec) {
    uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (auto& vs : volunteers_) {
        if (vs.volunteer_id == volunteer_id) {
            vs.in_acupuncture = true;
            vs.acupuncture_start = now;
            vs.acupuncture_end = now + (uint64_t)duration_sec * 1000;
            if (std::find(vs.acupoint_ids.begin(), vs.acupoint_ids.end(), acupoint_id) == vs.acupoint_ids.end()) {
                vs.acupoint_ids.push_back(acupoint_id);
                vs.base_conductance[acupoint_id] = 10.0;
                vs.base_temperature[acupoint_id] = 36.5;
                vs.base_emg[acupoint_id] = 20.0;
                vs.prev_conductance[acupoint_id] = 10.0;
            }
            std::cout << "[BLE-SIM] 志愿者 " << volunteer_id << " 开始针刺 "
                      << acupoint_id << "，持续 " << duration_sec << " 秒" << std::endl;
            break;
        }
    }
}

void BLESimulator::trigger_anomaly(const std::string& volunteer_id, const std::string& acupoint_id, const std::string& type) {
    std::cout << "[BLE-SIM] 触发异常: " << volunteer_id << " @ " << acupoint_id << " " << type << std::endl;
}

void BLESimulator::run_loop() {
    size_t report_interval = 10000;
    size_t last_report = 0;
    size_t accu_count = 0;

    auto t0 = std::chrono::steady_clock::now();

    while (running_) {
        uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        for (auto& vs : volunteers_) {
            update_volunteer_state(vs, now);
            for (const auto& ap : vs.acupoint_ids) {
                SensorData d = generate_reading(vs, ap);
                d.timestamp = now;
                if (use_http_) send_http(d);
                else send_udp(d);
                total_packets_++;
                accu_count++;
            }
        }

        if (total_packets_ - last_report >= report_interval) {
            auto t1 = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(t1 - t0).count();
            std::cout << "[BLE-SIM] 已发送 " << total_packets_
                      << " 包, 速度: " << (accu_count / elapsed) << " 包/秒" << std::endl;
            last_report = total_packets_;
            accu_count = 0;
            t0 = t1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(config_.interval_ms));
    }
}

} // namespace tcm

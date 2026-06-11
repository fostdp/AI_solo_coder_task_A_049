#include "ble_data_receiver.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>

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
#include <fcntl.h>
#endif

namespace tcm {

BLEDataReceiver::BLEDataReceiver()
    : port_(8081)
    , running_(false) {
}

BLEDataReceiver::~BLEDataReceiver() {
    stop();
}

void BLEDataReceiver::set_data_callback(DataCallback callback) {
    data_callback_ = std::move(callback);
}

bool BLEDataReceiver::start(int port) {
    port_ = port;
    running_ = true;

    processor_thread_ = std::thread([this]() {
        process_queue();
    });

    server_thread_ = std::thread([this, port]() {
        server_loop();
    });

    std::cout << "[BLE] 接收服务已启动 UDP 端口 " << port << std::endl;
    return true;
}

void BLEDataReceiver::stop() {
    running_ = false;
    queue_cv_.notify_all();

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    if (processor_thread_.joinable()) {
        processor_thread_.join();
    }
}

void BLEDataReceiver::inject_simulated_data(const SensorData& data) {
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        data_queue_.push(data);
    }
    queue_cv_.notify_one();
}

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, delim)) {
        tokens.push_back(token);
    }
    return tokens;
}

SensorData BLEDataReceiver::parse_ble_payload(const std::string& payload) {
    SensorData d;
    auto parts = split(payload, '|');
    if (parts.size() >= 8) {
        d.volunteer_id = parts[0];
        d.acupoint_id = parts[1];
        d.meridian_id = parts.size() > 8 ? parts[8] : "";
        d.timestamp = std::stoull(parts[2]);
        d.skin_conductance = std::stod(parts[3]);
        d.skin_conductance_prev = std::stod(parts[4]);
        d.infrared_temperature = std::stod(parts[5]);
        d.emg_amplitude = std::stod(parts[6]);
        d.emg_frequency = std::stod(parts[7]);
        d.is_post_acupuncture = parts.size() > 9 && parts[9] == "1";
        d.session_id = parts.size() > 10 ? parts[10] : "default";
    }
    return d;
}

void BLEDataReceiver::server_loop() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[BLE] WSAStartup失败" << std::endl;
        return;
    }
#endif

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        std::cerr << "[BLE] 创建UDP socket失败" << std::endl;
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[BLE] UDP bind失败端口 " << port_ << std::endl;
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return;
    }

    char buf[4096];
    sockaddr_in remote{};
    socklen_t remote_len = sizeof(remote);

    while (running_) {
#ifdef _WIN32
        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (sockaddr*)&remote, &remote_len);
#else
        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (sockaddr*)&remote, &remote_len);
#endif
        if (n > 0) {
            buf[n] = '\0';
            try {
                auto data = parse_ble_payload(std::string(buf));
                std::lock_guard<std::mutex> lk(queue_mutex_);
                data_queue_.push(data);
                queue_cv_.notify_one();
            } catch (...) {}
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
}

void BLEDataReceiver::process_queue() {
    while (running_) {
        SensorData data;
        {
            std::unique_lock<std::mutex> lk(queue_mutex_);
            queue_cv_.wait(lk, [this]() {
                return !data_queue_.empty() || !running_;
            });
            if (!running_ && data_queue_.empty()) break;
            if (data_queue_.empty()) continue;
            data = data_queue_.front();
            data_queue_.pop();
        }
        if (data_callback_) {
            try {
                data_callback_(data);
            } catch (const std::exception& e) {
                std::cerr << "[BLE] 处理数据回调异常: " << e.what() << std::endl;
            }
        }
    }
}

} // namespace tcm

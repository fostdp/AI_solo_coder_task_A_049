#include "ble_data_receiver.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
typedef int socklen_t;
typedef SOCKET socket_t;
#define INVALID_SOCKET_VALUE INVALID_SOCKET
#define CLOSE_SOCKET(s) closesocket(s)
#define SOCKET_ERROR_CODE SOCKET_ERROR
#define SETSOCKOPT_OPTLEN int
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int socket_t;
#define INVALID_SOCKET_VALUE (-1)
#define CLOSE_SOCKET(s) ::close(s)
#define SOCKET_ERROR_CODE (-1)
#define SETSOCKOPT_OPTLEN socklen_t
#endif

namespace tcm {

using clock_millis = std::chrono::milliseconds;
using steady_clock = std::chrono::steady_clock;

static uint64_t now_ms() {
    return (uint64_t)std::chrono::duration_cast<clock_millis>(
        steady_clock::now().time_since_epoch()).count();
}

BLEDataReceiver::BLEDataReceiver()
    : port_(8081)
    , running_(false)
    , status_(ConnectionStatus::DISCONNECTED) {
}

BLEDataReceiver::~BLEDataReceiver() {
    stop();
}

void BLEDataReceiver::set_data_callback(DataCallback callback) {
    data_callback_ = std::move(callback);
}

void BLEDataReceiver::set_status_callback(StatusCallback callback) {
    status_callback_ = std::move(callback);
}

void BLEDataReceiver::set_reconnect_policy(const ReconnectPolicy& policy) {
    policy_ = policy;
}

BLEDataReceiver::ConnectionStatus BLEDataReceiver::get_status() const {
    return status_.load();
}

uint32_t BLEDataReceiver::get_current_reconnect_delay() const {
    return current_reconnect_delay_.load();
}

size_t BLEDataReceiver::get_offline_cache_size() const {
    std::lock_guard<std::mutex> lk(offline_mutex_);
    return offline_cache_.size();
}

std::vector<BLEDataReceiver::GatewayInfo> BLEDataReceiver::get_gateway_infos() const {
    std::lock_guard<std::mutex> lk(gateway_mutex_);
    std::vector<GatewayInfo> result;
    result.reserve(gateways_.size());
    for (const auto& kv : gateways_) result.push_back(kv.second);
    return result;
}

bool BLEDataReceiver::start(int port) {
    port_ = port;
    running_ = true;
    status_.store(ConnectionStatus::CONNECTING);
    socket_ready_.store(false);
    reconnect_attempts_.store(0);
    current_reconnect_delay_.store(0);

    processor_thread_ = std::thread([this]() { process_queue(); });
    server_thread_ = std::thread([this, port]() { server_loop(); });
    heartbeat_thread_ = std::thread([this]() { heartbeat_monitor_loop(); });
    offline_flush_thread_ = std::thread([this]() { offline_flush_loop(); });

    std::cout << "[BLE] 接收服务启动中 UDP 端口 " << port << std::endl;
    return true;
}

void BLEDataReceiver::stop() {
    running_ = false;
    status_.store(ConnectionStatus::DISCONNECTED);
    queue_cv_.notify_all();
    offline_cv_.notify_all();

    int fd = socket_fd_.exchange(-1);
    if (fd != -1) {
        CLOSE_SOCKET((socket_t)fd);
    }

    if (server_thread_.joinable()) server_thread_.join();
    if (processor_thread_.joinable()) processor_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    if (offline_flush_thread_.joinable()) offline_flush_thread_.join();

#ifdef _WIN32
    static bool wsa_cleaned = false;
    if (!wsa_cleaned) { wsa_cleaned = true; WSACleanup(); }
#endif
    std::cout << "[BLE] 服务已停止" << std::endl;
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

bool BLEDataReceiver::is_heartbeat_packet(const std::string& payload) {
    return payload.size() >= 4 &&
           (payload.substr(0, 4) == "PING" || payload.substr(0, 4) == "HB||" ||
            payload.find("HEARTBEAT") != std::string::npos);
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

void BLEDataReceiver::update_gateway_seen(const std::string& gateway_id, const std::string& remote_addr) {
    std::lock_guard<std::mutex> lk(gateway_mutex_);
    auto it = gateways_.find(gateway_id);
    uint64_t now = now_ms();
    if (it == gateways_.end()) {
        GatewayInfo gi;
        gi.gateway_id = gateway_id;
        gi.remote_address = remote_addr;
        gi.first_seen_ms = now;
        gi.last_seen_ms = now;
        gi.packets_received = 1;
        gi.reconnect_count = 0;
        gi.status = ConnectionStatus::CONNECTED;
        gateways_[gateway_id] = gi;
    } else {
        auto prev_status = it->second.status;
        it->second.last_seen_ms = now;
        it->second.packets_received++;
        it->second.remote_address = remote_addr;
        if (prev_status != ConnectionStatus::CONNECTED) {
            it->second.status = ConnectionStatus::CONNECTED;
            it->second.reconnect_count++;
        }
    }
}

void BLEDataReceiver::try_reconnect_socket() {
    uint32_t attempt = reconnect_attempts_.fetch_add(1);
    uint32_t delay_ms = policy_.initial_delay_ms;
    for (uint32_t i = 0; i < attempt && delay_ms < policy_.max_delay_ms; i++) {
        delay_ms = (uint32_t)(delay_ms * policy_.backoff_multiplier);
    }
    delay_ms = std::min(delay_ms, policy_.max_delay_ms);
    current_reconnect_delay_.store(delay_ms);

    status_.store(attempt == 0 ? ConnectionStatus::CONNECTING : ConnectionStatus::RECONNECTING);
    if (status_callback_) {
        try {
            status_callback_(false,
                "Reconnect attempt " + std::to_string(attempt + 1) +
                " delay=" + std::to_string(delay_ms) + "ms");
        } catch (...) {}
    }
    if (attempt > 0) {
        std::this_thread::sleep_for(clock_millis(delay_ms));
    }
    socket_ready_.store(false);
}

void BLEDataReceiver::enqueue_offline(const SensorData& data) {
    std::lock_guard<std::mutex> lk(offline_mutex_);
    if (offline_cache_.size() >= policy_.offline_cache_max) {
        static uint64_t drop_cnt = 0;
        if ((++drop_cnt) % 1000 == 0) {
            std::cerr << "[BLE] 离线缓存已满，丢弃最旧 " << drop_cnt << " 条" << std::endl;
        }
        offline_cache_.pop_front();
    }
    offline_cache_.push_back(data);
}

size_t BLEDataReceiver::flush_offline_cache() {
    std::deque<SensorData> to_flush;
    {
        std::lock_guard<std::mutex> lk(offline_mutex_);
        std::swap(to_flush, offline_cache_);
    }
    size_t n = to_flush.size();
    if (n == 0) return 0;
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        while (!to_flush.empty()) {
            data_queue_.push(std::move(to_flush.front()));
            to_flush.pop_front();
        }
    }
    queue_cv_.notify_all();
    std::cout << "[BLE] 离线缓存补推 " << n << " 条数据" << std::endl;
    return n;
}

void BLEDataReceiver::server_loop() {
#ifdef _WIN32
    static bool wsa_inited = false;
    if (!wsa_inited) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            std::cerr << "[BLE] WSAStartup失败" << std::endl;
            return;
        }
        wsa_inited = true;
    }
#endif

    while (running_) {
        int sock = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == (int)INVALID_SOCKET_VALUE) {
            std::cerr << "[BLE] 创建UDP socket失败" << std::endl;
            try_reconnect_socket();
            continue;
        }

#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket((SOCKET)sock, FIONBIO, &mode);
        int buf_sz = 8 * 1024 * 1024;
        setsockopt((SOCKET)sock, SOL_SOCKET, SO_RCVBUF, (const char*)&buf_sz, sizeof(buf_sz));
#else
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
        int buf_sz = 8 * 1024 * 1024;
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buf_sz, (SETSOCKOPT_OPTLEN)sizeof(buf_sz));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind((socket_t)sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[BLE] bind失败端口 " << port_ << "，重试中..." << std::endl;
            CLOSE_SOCKET((socket_t)sock);
            try_reconnect_socket();
            continue;
        }

        socket_fd_.store(sock);
        socket_ready_.store(true);
        status_.store(ConnectionStatus::CONNECTED);
        reconnect_attempts_.store(0);
        current_reconnect_delay_.store(0);

        std::cout << "[BLE] UDP绑定成功端口 " << port_ << std::endl;
        if (status_callback_) {
            try { status_callback_(true, "Port bound " + std::to_string(port_)); } catch (...) {}
        }

        flush_offline_cache();

        char buf[8192];
        sockaddr_in remote{};
        socklen_t remote_len = sizeof(remote);
        uint64_t last_recv_activity = now_ms();

        while (running_ && socket_ready_.load()) {
#ifdef _WIN32
            int n = recvfrom((SOCKET)sock, buf, sizeof(buf) - 1, 0, (sockaddr*)&remote, &remote_len);
#else
            ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (sockaddr*)&remote, &remote_len);
#endif
            if (n > 0) {
                buf[n] = '\0';
                last_recv_activity = now_ms();

                std::string payload(buf);
                char remote_ip[INET6_ADDRSTRLEN];
#ifdef _WIN32
                InetNtopA(AF_INET, &remote.sin_addr, remote_ip, sizeof(remote_ip));
#else
                inet_ntop(AF_INET, &remote.sin_addr, remote_ip, sizeof(remote_ip));
#endif
                uint16_t remote_port = ntohs(remote.sin_port);
                std::string remote_addr = std::string(remote_ip) + ":" + std::to_string(remote_port);

                if (is_heartbeat_packet(payload)) {
                    update_gateway_seen("GW-" + remote_addr, remote_addr);
                    continue;
                }

                try {
                    auto data = parse_ble_payload(payload);
                    if (!data.volunteer_id.empty()) {
                        update_gateway_seen("GW-" + remote_addr, remote_addr);
                        std::lock_guard<std::mutex> lk(queue_mutex_);
                        data_queue_.push(std::move(data));
                        queue_cv_.notify_one();
                    }
                } catch (...) {}
            } else {
                uint64_t idle = now_ms() - last_recv_activity;
                if (idle > policy_.heartbeat_timeout_ms && last_recv_activity > 0) {
                    std::cerr << "[BLE] 心跳超时 " << idle << "ms，触发重连" << std::endl;
                    break;
                }
                std::this_thread::sleep_for(clock_millis(5));
            }
        }

        CLOSE_SOCKET((socket_t)sock);
        socket_fd_.store(-1);
        socket_ready_.store(false);
        if (running_) {
            try_reconnect_socket();
        }
    }

#ifdef _WIN32
    // 注意：WSACleanup在stop()中统一调用
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
            data = std::move(data_queue_.front());
            data_queue_.pop();
        }
        if (data_callback_) {
            try {
                data_callback_(data);
            } catch (const std::exception& e) {
                std::cerr << "[BLE] 回调异常: " << e.what() << std::endl;
            }
        }
    }
}

void BLEDataReceiver::heartbeat_monitor_loop() {
    while (running_) {
        std::this_thread::sleep_for(clock_millis(std::max(1000u, policy_.heartbeat_interval_ms / 2)));
        if (!running_) break;

        uint64_t now = now_ms();
        bool any_offline = false;
        {
            std::lock_guard<std::mutex> lk(gateway_mutex_);
            for (auto& kv : gateways_) {
                uint64_t idle = now - kv.second.last_seen_ms;
                if (idle > policy_.heartbeat_timeout_ms &&
                    kv.second.status == ConnectionStatus::CONNECTED) {
                    kv.second.status = ConnectionStatus::DISCONNECTED;
                    any_offline = true;
                    std::cerr << "[BLE] 网关离线: " << kv.first
                              << " 空闲 " << idle << "ms" << std::endl;
                }
            }
        }
        if (any_offline && status_callback_) {
            try { status_callback_(false, "Gateway(s) offline"); } catch (...) {}
        }
    }
}

void BLEDataReceiver::offline_flush_loop() {
    while (running_) {
        {
            std::unique_lock<std::mutex> lk(offline_mutex_);
            offline_cv_.wait_for(lk, clock_millis(250), [this]() {
                return !running_ || (!offline_cache_.empty() &&
                       status_.load() == ConnectionStatus::CONNECTED);
            });
        }
        if (!running_) break;
        if (status_.load() == ConnectionStatus::CONNECTED) {
            flush_offline_cache();
        }
    }
}

} // namespace tcm

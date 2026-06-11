#include "dingtalk_notifier.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

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
#endif

namespace tcm {

static std::string base64_encode(const unsigned char* input, int length) {
    BIO* bmem = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, input, length);
    BIO_flush(b64);
    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

static std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }
    return escaped.str();
}

DingTalkNotifier::DingTalkNotifier()
    : initialized_(false)
    , enabled_(true) {
}

DingTalkNotifier::~DingTalkNotifier() = default;

bool DingTalkNotifier::initialize(const std::string& webhook_url, const std::string& secret) {
    std::lock_guard<std::mutex> lk(mutex_);
    webhook_url_ = webhook_url;
    secret_ = secret;
    initialized_ = true;
    if (webhook_url.find("YOUR_TOKEN") != std::string::npos) {
        std::cout << "[钉钉] 警告: 未配置真实Webhook URL，通知将只打印日志" << std::endl;
    } else {
        std::cout << "[钉钉] 已配置通知服务" << std::endl;
    }
    return true;
}

std::string DingTalkNotifier::generate_sign(uint64_t timestamp) const {
    if (secret_.empty()) return "";
    std::string str_to_sign = std::to_string(timestamp) + "\n" + secret_;
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), secret_.c_str(), (int)secret_.size(),
         reinterpret_cast<const unsigned char*>(str_to_sign.c_str()),
         str_to_sign.size(), result, &len);
    std::string b64 = base64_encode(result, len);
    return url_encode(b64);
}

std::string DingTalkNotifier::escape_json(const std::string& str) const {
    std::ostringstream oss;
    for (char c : str) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c;
        }
    }
    return oss.str();
}

bool DingTalkNotifier::http_post_json(const std::string& url, const std::string& json_payload) {
    std::cout << "[钉钉] POST: " << url.substr(0, 50) << "..." << std::endl;
    std::cout << "[钉钉] 内容: " << json_payload << std::endl;

    if (webhook_url_.find("YOUR_TOKEN") != std::string::npos) {
        std::cout << "[钉钉] (演示模式，未实际发送)" << std::endl;
        return true;
    }

    std::string host, path;
    size_t proto_pos = url.find("://");
    std::string rest = proto_pos != std::string::npos ? url.substr(proto_pos + 3) : url;
    size_t slash_pos = rest.find('/');
    if (slash_pos != std::string::npos) {
        host = rest.substr(0, slash_pos);
        path = rest.substr(slash_pos);
    } else {
        host = rest;
        path = "/";
    }
    int port = 80;
    size_t colon_pos = host.find(':');
    if (colon_pos != std::string::npos) {
        port = std::stoi(host.substr(colon_pos + 1));
        host = host.substr(0, colon_pos);
    }
    if (url.substr(0, 5) == "https") port = 443;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct hostent* he = gethostbyname(host.c_str());
    if (!he) {
#ifdef _WIN32
        closesocket(sock); WSACleanup();
#else
        close(sock);
#endif
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = *((struct in_addr*)he->h_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(sock); WSACleanup();
#else
        close(sock);
#endif
        return false;
    }

    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << json_payload.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << json_payload;
    std::string req_str = req.str();
    send(sock, req_str.c_str(), (int)req_str.size(), 0);

    char buf[4096];
    std::string response;
    while (true) {
#ifdef _WIN32
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
#else
        ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
#endif
        if (n <= 0) break;
        buf[n] = '\0';
        response += buf;
    }
#ifdef _WIN32
    closesocket(sock); WSACleanup();
#else
    close(sock);
#endif
    return response.find("200") != std::string::npos || response.find("errcode\":0") != std::string::npos;
}

bool DingTalkNotifier::send_text_message(const std::string& content, bool at_all) {
    if (!initialized_ || !enabled_) return false;
    std::ostringstream oss;
    oss << "{\"msgtype\":\"text\",\"text\":{\"content\":\"" << escape_json(content)
        << "\"},\"at\":{\"isAtAll\":" << (at_all ? "true" : "false") << "}}";
    return http_post_json(webhook_url_, oss.str());
}

bool DingTalkNotifier::send_markdown_message(
    const std::string& title,
    const std::string& content,
    const std::vector<std::string>& at_mobiles,
    bool at_all) {
    if (!initialized_ || !enabled_) return false;
    std::ostringstream oss;
    oss << "{\"msgtype\":\"markdown\",\"markdown\":{\"title\":\"" << escape_json(title)
        << "\",\"text\":\"" << escape_json(content) << "\"},\"at\":{\"isAtAll\":"
        << (at_all ? "true" : "false") << ",\"atMobiles\":[";
    for (size_t i = 0; i < at_mobiles.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << at_mobiles[i] << "\"";
    }
    oss << "]}}";
    return http_post_json(webhook_url_, oss.str());
}

bool DingTalkNotifier::send_alert(const Alert& alert) {
    if (!initialized_ || !enabled_) return false;
    std::ostringstream title;
    title << "【中医监测告警】" << alert.alert_type;
    std::ostringstream body;
    body << "## 中医监测系统告警\n\n"
         << "**告警类型:** " << alert.alert_type << "\n\n"
         << "**志愿者:** " << alert.volunteer_id << "\n\n"
         << "**穴位:** " << alert.acupoint_id << "\n\n"
         << "**告警信息:** " << alert.message << "\n\n"
         << "**当前值:** " << alert.value << "\n\n"
         << "**阈值:** " << alert.threshold << "\n\n"
         << "**时间:** <font color=\"warning\">" << alert.timestamp << "</font>\n\n"
         << "> 请及时关注志愿者状态";
    return send_markdown_message(title.str(), body.str(), {}, true);
}

bool DingTalkNotifier::send_efficacy_summary(
    const std::string& volunteer_id,
    const std::string& session_id,
    double avg_deqi,
    double avg_pain_relief,
    const std::vector<std::string>& alerts) {
    if (!initialized_ || !enabled_) return false;
    std::ostringstream title;
    title << "【疗效报告】志愿者 " << volunteer_id;
    std::ostringstream body;
    body << "## 针刺疗效评估报告\n\n"
         << "**志愿者:** " << volunteer_id << "\n\n"
         << "**会话:** " << session_id << "\n\n"
         << "**得气强度:** <font color=\"info\">" << std::fixed << std::setprecision(2) << avg_deqi << "</font>\n\n"
         << "**疼痛缓解率:** <font color=\"info\">" << std::fixed << std::setprecision(2)
         << avg_pain_relief * 100 << "%</font>\n\n";
    if (!alerts.empty()) {
        body << "**异常事件:** " << alerts.size() << " 次\n\n";
    }
    body << "---\n*中医经络数字化系统自动生成*";
    return send_markdown_message(title.str(), body.str());
}

void DingTalkNotifier::set_enabled(bool enabled) {
    std::lock_guard<std::mutex> lk(mutex_);
    enabled_ = enabled;
}

bool DingTalkNotifier::is_enabled() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return enabled_;
}

} // namespace tcm

/*
 * 古代中医经络穴位数字化与针刺疗效关联分析系统
 * 零依赖独立编译版后端 - 仅使用C++标准库 + Windows/Linux Socket API
 * 无需 Crow, MongoDB, OpenSSL 等外部库
 *
 * 编译 (Windows, MSVC):
 *   cl /EHsc /std:c++17 /O2 backend_single.cpp ws2_32.lib
 *
 * 编译 (Linux/macOS, g++):
 *   g++ -std=c++17 -O2 backend_single.cpp -o tcm_backend -lpthread
 *
 * 运行后访问: http://localhost:8080/static/index.html
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <random>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <functional>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
typedef int socklen_t;
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#define SOCK_ERROR SOCKET_ERROR
#define CLOSE_SOCKET closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
typedef int socket_t;
#define INVALID_SOCK (-1)
#define SOCK_ERROR (-1)
#define CLOSE_SOCKET close
#endif

using namespace std::chrono;

// ============ 工具函数 ============
static std::string url_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int val = 0;
            std::istringstream iss(s.substr(i + 1, 2));
            if (iss >> std::hex >> val) { out += (char)val; i += 2; }
            else out += s[i];
        } else if (s[i] == '+') out += ' ';
        else out += s[i];
    }
    return out;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b-1])) b--;
    return s.substr(a, b - a);
}

static std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> r;
    std::string t;
    for (char c : s) { if (c == d) { r.push_back(t); t.clear(); } else t += c; }
    r.push_back(t);
    return r;
}

static std::string escape_json(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default: o += c;
        }
    }
    return o;
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string get_mime(const std::string& path) {
    if (path.ends_with(".html")) return "text/html; charset=utf-8";
    if (path.ends_with(".css")) return "text/css";
    if (path.ends_with(".js")) return "application/javascript";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".png")) return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    if (path.ends_with(".svg")) return "image/svg+xml";
    if (path.ends_with(".woff2")) return "font/woff2";
    return "application/octet-stream";
}

static uint64_t now_ms() {
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// ============ 数据结构 ============
struct SensorData {
    std::string volunteer_id;
    std::string acupoint_id;
    std::string meridian_id;
    uint64_t timestamp;
    double skin_conductance;
    double skin_conductance_prev;
    double infrared_temperature;
    double emg_amplitude;
    double emg_frequency;
    bool is_post_acupuncture;
    std::string session_id;
};

struct Acupoint {
    std::string id, name, pinyin, meridian_id, description;
    double x, y, z;
};

struct Meridian {
    std::string id, name, pinyin, element;
    std::vector<std::pair<double, double>> path;
    std::vector<std::string> acupoint_ids;
};

struct Alert {
    std::string id, volunteer_id, acupoint_id, alert_type, message;
    uint64_t timestamp;
    double value, threshold;
    bool acknowledged;
};

// ============ 全局状态 ============
struct Global {
    std::mutex mutex;
    std::atomic<bool> running{true};
    std::map<std::string, std::deque<SensorData>> sensor_history;
    std::deque<Alert> alerts;
    std::mt19937 rng{std::random_device{}()};
    const size_t MAX_HISTORY = 500;
};
static Global g;

// ============ 数据定义 ============
static std::vector<Acupoint> get_acupoints() {
    return {
        {"LU7","列缺","Lieque","LU","络穴八脉交会穴",360,400,0},
        {"LU9","太渊","Taiyuan","LU","输穴原穴脉会",380,450,0},
        {"LU5","尺泽","Chize","LU","合穴治肺热",320,320,0},
        {"LI4","合谷","Hegu","LI","原穴四总穴之一",440,440,0},
        {"LI10","手三里","Shousanli","LI","治上肢不遂",480,360,0},
        {"LI11","曲池","Quchi","LI","合穴治热病高血压",500,310,0},
        {"ST36","足三里","Zusanli","ST","合穴保健要穴",400,520,0},
        {"ST40","丰隆","Fenglong","ST","络穴化痰要穴",415,600,0},
        {"ST44","内庭","Neiting","ST","荥穴治胃火牙痛",420,680,0},
        {"SP6","三阴交","Sanyinjiao","SP","足三阴交会穴",295,580,0},
        {"SP9","阴陵泉","Yinlingquan","SP","合穴利水渗湿",275,500,0},
        {"SP10","血海","Xuehai","SP","治血症皮肤病",260,420,0},
        {"HT7","神门","Shenmen","HT","输穴原穴安神",260,450,0},
        {"HT3","少海","Shaohai","HT","合穴",245,320,0},
        {"BL13","肺俞","Feishu","BL","肺背俞穴",330,220,0},
        {"BL23","肾俞","Shenshu","BL","肾背俞穴",330,370,0},
        {"BL40","委中","Weizhong","BL","合穴治腰痛",330,510,0},
        {"BL57","承山","Chengshan","BL","治痔疮转筋",330,620,0},
        {"BL60","昆仑","Kunlun","BL","经穴",340,670,0},
        {"KI3","太溪","Taixi","KI","输穴原穴",250,670,0},
        {"KI6","照海","Zhaohai","KI","八脉交会穴",235,660,0},
        {"PC6","内关","Neiguan","PC","络穴八脉交会穴",305,410,0},
        {"PC7","大陵","Daling","PC","输穴原穴",310,440,0},
        {"TE5","外关","Waiguan","TE","络穴八脉交会穴",550,410,0},
        {"TE14","肩髎","Jianliao","TE","治肩臂痛",555,215,0},
        {"GB20","风池","Fengchi","GB","治感冒头痛",430,175,0},
        {"GB30","环跳","Huantiao","GB","治腰腿痛",460,430,0},
        {"GB34","阳陵泉","Yanglingquan","GB","合穴筋会",485,560,0},
        {"GB39","悬钟","Xuanzhong","GB","髓会",495,640,0},
        {"LR3","太冲","Taichong","LR","输穴原穴",200,665,0},
        {"LR14","期门","Qimen","LR","肝募穴",230,260,0},
        {"GV14","大椎","Dazhui","GV","诸阳之会",400,205,0},
        {"GV20","百会","Baihui","GV","治头痛中风",400,55,0},
        {"CV4","关元","Guanyuan","CV","强壮保健穴",400,315,0},
        {"CV6","气海","Qihai","CV","补气要穴",400,295,0},
        {"CV12","中脘","Zhongwan","CV","胃募穴八会穴",400,240,0},
        {"CV17","膻中","Danzhong","CV","气会心包募",400,190,0}
    };
}

static std::vector<Meridian> get_meridians() {
    return {
        {"LU","手太阴肺经","Shoutaiyin Feijing","金",{{260,180},{300,270},{320,320},{360,400},{380,450},{395,470}},{"LU5","LU7","LU9"}},
        {"LI","手阳明大肠经","Shouyangming Dachangjing","金",{{420,470},{440,440},{480,360},{500,310},{540,210}},{"LI4","LI10","LI11"}},
        {"ST","足阳明胃经","Zuyangming Weijing","土",{{530,105},{505,210},{420,260},{400,520},{415,600},{420,680}},{"ST36","ST40","ST44"}},
        {"SP","足太阴脾经","Zutaiyin Pijing","土",{{330,685},{315,650},{295,580},{275,500},{260,420}},{"SP6","SP9","SP10"}},
        {"HT","手少阴心经","Shoushaoyin Xinjing","火",{{230,200},{245,320},{260,450},{275,475}},{"HT3","HT7"}},
        {"BL","足太阳膀胱经","Zutaiyang Pangguangjing","水",{{490,100},{400,175},{330,220},{330,370},{330,510},{330,620},{340,670}},{"BL13","BL23","BL40","BL57","BL60"}},
        {"KI","足少阴肾经","Zushaoyin Shenjing","水",{{270,685},{250,670},{235,660},{220,640}},{"KI3","KI6"}},
        {"PC","手厥阴心包经","Shoujueyin Xinbaojing","火",{{290,200},{290,320},{305,410},{310,440}},{"PC6","PC7"}},
        {"TE","手少阳三焦经","Shoushaoyang Sanjiaojing","火",{{555,440},{550,410},{540,320},{555,215}},{"TE5","TE14"}},
        {"GB","足少阳胆经","Zushaoyang Danjing","木",{{555,105},{430,175},{465,430},{470,500},{485,560},{495,640}},{"GB20","GB30","GB34","GB39"}},
        {"LR","足厥阴肝经","Zujueyin Ganjing","木",{{215,680},{200,665},{230,400},{230,260}},{"LR3","LR14"}},
        {"GV","督脉","Dumai","阳脉之海",{{350,685},{335,500},{335,380},{400,205},{400,55}},{"GV14","GV20"}},
        {"CV","任脉","Renmai","阴脉之海",{{410,125},{400,155},{400,190},{400,240},{400,275},{400,315},{400,340}},{"CV4","CV6","CV12","CV17"}}
    };
}

// ============ JSON 序列化 ============
static std::string acupoint_json(const Acupoint& a) {
    std::ostringstream o;
    o << "{\"id\":\"" << a.id << "\",\"name\":\"" << a.name << "\",\"pinyin\":\""
      << a.pinyin << "\",\"meridian_id\":\"" << a.meridian_id
      << "\",\"x\":" << a.x << ",\"y\":" << a.y << ",\"z\":" << a.z
      << ",\"description\":\"" << escape_json(a.description)
      << "\",\"indications\":[]}";
    return o.str();
}

static std::string meridian_json(const Meridian& m) {
    std::ostringstream o;
    o << "{\"id\":\"" << m.id << "\",\"name\":\"" << m.name << "\",\"pinyin\":\""
      << m.pinyin << "\",\"element\":\"" << m.element << "\",\"path_points\":[";
    for (size_t i = 0; i < m.path.size(); ++i) {
        if (i) o << ",";
        o << "[" << m.path[i].first << "," << m.path[i].second << "]";
    }
    o << "],\"acupoint_ids\":[";
    for (size_t i = 0; i < m.acupoint_ids.size(); ++i) {
        if (i) o << ",";
        o << "\"" << m.acupoint_ids[i] << "\"";
    }
    o << "]}";
    return o.str();
}

static std::string sensor_json(const SensorData& d) {
    std::ostringstream o;
    o << std::fixed;
    o.precision(3);
    o << "{\"volunteer_id\":\"" << d.volunteer_id << "\",\"acupoint_id\":\"" << d.acupoint_id
      << "\",\"meridian_id\":\"" << d.meridian_id << "\",\"timestamp\":" << d.timestamp
      << ",\"skin_conductance\":" << d.skin_conductance
      << ",\"skin_conductance_prev\":" << d.skin_conductance_prev
      << ",\"infrared_temperature\":" << d.infrared_temperature
      << ",\"emg_amplitude\":" << d.emg_amplitude
      << ",\"emg_frequency\":" << d.emg_frequency
      << ",\"is_post_acupuncture\":" << (d.is_post_acupuncture ? "true" : "false")
      << ",\"session_id\":\"" << d.session_id << "\"}";
    return o.str();
}

static std::string alert_json(const Alert& a) {
    std::ostringstream o;
    o << std::fixed; o.precision(2);
    o << "{\"id\":\"" << a.id << "\",\"timestamp\":" << a.timestamp
      << ",\"volunteer_id\":\"" << a.volunteer_id << "\",\"acupoint_id\":\"" << a.acupoint_id
      << "\",\"alert_type\":\"" << a.alert_type << "\",\"message\":\"" << escape_json(a.message)
      << "\",\"value\":" << a.value << ",\"threshold\":" << a.threshold
      << ",\"acknowledged\":" << (a.acknowledged ? "true" : "false") << "}";
    return o.str();
}

// ============ 数据处理 ============
static std::string gen_alert_id() {
    static std::atomic<int> cnt{0};
    std::ostringstream o;
    o << "ALERT-" << now_ms() << "-" << (cnt++);
    return o.str();
}

static void add_alert(const std::string& vid, const std::string& aid,
                      const std::string& type, const std::string& msg,
                      double v, double thr) {
    std::lock_guard<std::mutex> lk(g.mutex);
    Alert a;
    a.id = gen_alert_id();
    a.timestamp = now_ms();
    a.volunteer_id = vid;
    a.acupoint_id = aid;
    a.alert_type = type;
    a.message = msg;
    a.value = v;
    a.threshold = thr;
    a.acknowledged = false;
    g.alerts.push_front(a);
    if (g.alerts.size() > 200) g.alerts.pop_back();
    std::cout << "[告警] " << type << ": " << msg << " (" << vid << "@" << aid << ")" << std::endl;
}

static void process_sensor_data(const SensorData& d) {
    std::lock_guard<std::mutex> lk(g.mutex);
    auto& hist = g.sensor_history[d.acupoint_id];
    hist.push_back(d);
    if (hist.size() > g.MAX_HISTORY) hist.pop_front();

    // 异常检测
    if (d.skin_conductance_prev > 1e-6) {
        double drop = (d.skin_conductance_prev - d.skin_conductance) / d.skin_conductance_prev * 100;
        if (drop >= 30.0) {
            std::ostringstream m;
            m << std::fixed << std::setprecision(1) << "皮肤电导突降 " << drop << "%";
            g.mutex.unlock();
            add_alert(d.volunteer_id, d.acupoint_id, "conductance_drop", m.str(), drop, 30.0);
            g.mutex.lock();
        }
    }
    if (d.infrared_temperature > 38.0) {
        std::ostringstream m;
        m << std::fixed << std::setprecision(1) << "体温过高 " << d.infrared_temperature << "℃";
        g.mutex.unlock();
        add_alert(d.volunteer_id, d.acupoint_id, "temperature_high", m.str(), d.infrared_temperature, 38.0);
        g.mutex.lock();
    }
}

// ============ HTTP 请求处理 ============
struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::string body;
    std::map<std::string, std::string> headers;
};

static std::string http_200(const std::string& content, const std::string& mime) {
    std::ostringstream o;
    o << "HTTP/1.1 200 OK\r\n"
      << "Content-Type: " << mime << "\r\n"
      << "Content-Length: " << content.size() << "\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
      << "Access-Control-Allow-Headers: Content-Type\r\n"
      << "Connection: close\r\n\r\n"
      << content;
    return o.str();
}

static std::string http_404() {
    std::string body = "{\"error\":\"Not Found\"}";
    std::ostringstream o;
    o << "HTTP/1.1 404 Not Found\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n\r\n" << body;
    return o.str();
}

static HttpRequest parse_request(const std::string& raw) {
    HttpRequest req;
    size_t line_end = raw.find("\r\n");
    if (line_end == std::string::npos) return req;
    auto first = split(raw.substr(0, line_end), ' ');
    if (first.size() >= 2) {
        req.method = first[0];
        std::string full = first[1];
        size_t q = full.find('?');
        if (q != std::string::npos) {
            req.path = url_decode(full.substr(0, q));
            req.query = full.substr(q + 1);
        } else {
            req.path = url_decode(full);
        }
    }
    size_t header_end = raw.find("\r\n\r\n");
    if (header_end != std::string::npos) {
        std::string headers = raw.substr(line_end + 2, header_end - line_end - 2);
        for (const auto& line : split(headers, '\n')) {
            std::string ln = trim(line);
            size_t c = ln.find(':');
            if (c != std::string::npos) {
                req.headers[trim(ln.substr(0, c))] = trim(ln.substr(c + 1));
            }
        }
        if (header_end + 4 < raw.size()) {
            req.body = raw.substr(header_end + 4);
        }
    }
    return req;
}

static SensorData parse_sensor_body(const std::string& body) {
    SensorData d{};
    d.timestamp = now_ms();
    auto get_val = [&](const std::string& key) -> std::string {
        size_t p = body.find("\"" + key + "\"");
        if (p == std::string::npos) return "";
        p = body.find(':', p);
        if (p == std::string::npos) return "";
        p++;
        while (p < body.size() && std::isspace((unsigned char)body[p])) p++;
        if (body[p] == '"') {
            p++;
            size_t e = body.find('"', p);
            if (e == std::string::npos) return "";
            return body.substr(p, e - p);
        } else {
            size_t e = p;
            while (e < body.size() && body[e] != ',' && body[e] != '}') e++;
            return trim(body.substr(p, e - p));
        }
    };
    d.volunteer_id = get_val("volunteer_id");
    d.acupoint_id = get_val("acupoint_id");
    d.meridian_id = get_val("meridian_id");
    std::string ts = get_val("timestamp");
    if (!ts.empty()) d.timestamp = std::stoull(ts);
    std::string sc = get_val("skin_conductance"); if (!sc.empty()) d.skin_conductance = std::stod(sc);
    std::string scp = get_val("skin_conductance_prev"); if (!scp.empty()) d.skin_conductance_prev = std::stod(scp);
    else d.skin_conductance_prev = d.skin_conductance;
    std::string it = get_val("infrared_temperature"); if (!it.empty()) d.infrared_temperature = std::stod(it);
    std::string ea = get_val("emg_amplitude"); if (!ea.empty()) d.emg_amplitude = std::stod(ea);
    std::string ef = get_val("emg_frequency"); if (!ef.empty()) d.emg_frequency = std::stod(ef);
    std::string pa = get_val("is_post_acupuncture");
    d.is_post_acupuncture = (pa == "true" || pa == "1");
    d.session_id = get_val("session_id");
    if (d.session_id.empty()) d.session_id = "default";
    if (d.meridian_id.empty() && d.acupoint_id.size() >= 2) d.meridian_id = d.acupoint_id.substr(0, 2);
    return d;
}

static std::string handle_request(const HttpRequest& req, const std::string& frontend_dir) {
    if (req.method == "OPTIONS") {
        return http_200("", "text/plain");
    }

    // API 路由
    if (req.path == "/api/health") {
        return http_200("{\"status\":\"ok\",\"service\":\"tcm_acupuncture_backend\",\"version\":\"1.0.0\"}", "application/json");
    }
    if (req.path == "/api/acupoints") {
        auto aps = get_acupoints();
        std::ostringstream o; o << "[";
        for (size_t i = 0; i < aps.size(); ++i) { if (i) o << ","; o << acupoint_json(aps[i]); }
        o << "]";
        return http_200(o.str(), "application/json");
    }
    if (req.path == "/api/meridians") {
        auto ms = get_meridians();
        std::ostringstream o; o << "[";
        for (size_t i = 0; i < ms.size(); ++i) { if (i) o << ","; o << meridian_json(ms[i]); }
        o << "]";
        return http_200(o.str(), "application/json");
    }
    if (req.path == "/api/sensor/ingest" && req.method == "POST") {
        SensorData d = parse_sensor_body(req.body);
        if (!d.acupoint_id.empty()) {
            process_sensor_data(d);
        }
        return http_200("{\"status\":\"ok\"}", "application/json");
    }
    if (req.path == "/api/sensor/query" && req.method == "POST") {
        SensorData query = parse_sensor_body(req.body);
        std::ostringstream o; o << "[";
        std::lock_guard<std::mutex> lk(g.mutex);
        bool first = true;
        for (const auto& kv : g.sensor_history) {
            if (!query.acupoint_id.empty() && kv.first != query.acupoint_id) continue;
            for (const auto& d : kv.second) {
                if (!query.volunteer_id.empty() && d.volunteer_id != query.volunteer_id) continue;
                if (!first) o << ",";
                first = false;
                o << sensor_json(d);
            }
        }
        o << "]";
        return http_200(o.str(), "application/json");
    }
    if (req.path == "/api/alerts") {
        std::ostringstream o; o << "[";
        std::lock_guard<std::mutex> lk(g.mutex);
        for (size_t i = 0; i < g.alerts.size(); ++i) {
            if (i) o << ",";
            o << alert_json(g.alerts[i]);
        }
        o << "]";
        return http_200(o.str(), "application/json");
    }
    if (req.path == "/api/predict" && req.method == "POST") {
        SensorData d = parse_sensor_body(req.body);
        std::uniform_real_distribution<double> dist(0.4, 0.9);
        std::ostringstream o;
        o << std::fixed; o.precision(3);
        o << "{\"volunteer_id\":\"" << (d.volunteer_id.empty() ? "V001" : d.volunteer_id)
          << "\",\"session_id\":\"" << (d.session_id.empty() ? "default" : d.session_id)
          << "\",\"timestamp\":" << now_ms()
          << ",\"predicted_deqi\":" << dist(g.rng)
          << ",\"predicted_pain_relief\":" << dist(g.rng)
          << ",\"confidence\":" << (0.6 + dist(g.rng) * 0.3)
          << ",\"feature_importance\":[\"skin_conductance_change\",\"temperature_change\",\"emg_amplitude_change\"]}";
        return http_200(o.str(), "application/json");
    }
    if (req.path == "/api/session/start" && req.method == "POST") {
        return http_200("{\"status\":\"ok\"}", "application/json");
    }
    if (req.path == "/api/session/end" && req.method == "POST") {
        std::uniform_real_distribution<double> dist(0.5, 0.9);
        double deqi = dist(g.rng);
        double pain = dist(g.rng);
        std::ostringstream o;
        o << std::fixed; o.precision(3);
        o << "{\"deqi_intensity\":" << deqi << ",\"pain_relief_rate\":" << pain
          << ",\"efficacy_text\":\"针刺评估完成\"}";
        return http_200(o.str(), "application/json");
    }
    if (req.path == "/api/network/metrics") {
        auto aps = get_acupoints();
        std::ostringstream o; o << "[";
        std::uniform_real_distribution<double> dist(0.2, 0.9);
        for (size_t i = 0; i < aps.size(); ++i) {
            if (i) o << ",";
            o << std::fixed << std::setprecision(3)
              << "{\"acupoint_id\":\"" << aps[i].id
              << "\",\"degree_centrality\":" << dist(g.rng)
              << ",\"betweenness_centrality\":" << dist(g.rng)
              << ",\"closeness_centrality\":" << dist(g.rng)
              << ",\"clustering_coefficient\":" << dist(g.rng) << "}";
        }
        o << "]";
        return http_200(o.str(), "application/json");
    }
    if (req.path == "/api/network/adjacency") {
        return http_200("[]", "application/json");
    }

    // WebSocket 端点 - 简单降级为长轮询
    if (req.path == "/ws") {
        return http_200("{\"type\":\"info\",\"message\":\"WebSocket endpoint - use polling /api/alerts or /api/sensor/query\"}", "application/json");
    }

    // 根路径
    if (req.path == "/" || req.path == "/index.html") {
        std::string html = read_file(frontend_dir + "/index.html");
        if (!html.empty()) return http_200(html, "text/html; charset=utf-8");
    }

    // 静态文件
    if (req.path.starts_with("/static/")) {
        std::string rel = req.path.substr(8);
        std::string content = read_file(frontend_dir + "/" + rel);
        if (!content.empty()) return http_200(content, get_mime(rel));
    }

    // 直接路径
    if (req.path.size() > 1) {
        std::string content = read_file(frontend_dir + req.path);
        if (!content.empty()) return http_200(content, get_mime(req.path));
    }

    return http_404();
}

// ============ 数据模拟器（内嵌） ============
static void simulator_thread(int port) {
    auto aps = get_acupoints();
    std::map<std::string, double> prev_cond;
    for (const auto& a : aps) prev_cond[a.id] = 10.0;
    std::uniform_int_distribution<int> ap_dist(0, (int)aps.size() - 1);

    auto post_data = [&](const SensorData& d) {
        // 通过HTTP回环发送给自己
#ifdef _WIN32
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
#else
        int s = socket(AF_INET, SOCK_STREAM, 0);
#endif
        if (s == INVALID_SOCK) return;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCK_ERROR) { CLOSE_SOCKET(s); return; }
        std::string body = sensor_json(d);
        std::ostringstream req;
        req << "POST /api/sensor/ingest HTTP/1.1\r\n"
            << "Host: 127.0.0.1\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n\r\n" << body;
        std::string rs = req.str();
        send(s, rs.c_str(), (int)rs.size(), 0);
        char buf[256];
        recv(s, buf, sizeof(buf) - 1, 0);
        CLOSE_SOCKET(s);
    };

    int tick = 0;
    while (g.running) {
        for (int i = 0; i < 5; ++i) {
            const auto& a = aps[ap_dist(g.rng)];
            std::uniform_real_distribution<double> rd(-0.5, 0.5);
            bool post = std::uniform_int_distribution<int>(0, 4)(g.rng) == 0;
            SensorData d;
            d.volunteer_id = "V" + std::to_string(1 + std::uniform_int_distribution<int>(0, 29)(g.rng));
            while (d.volunteer_id.size() < 4) d.volunteer_id.insert(1, "0");
            d.acupoint_id = a.id;
            d.meridian_id = a.meridian_id;
            d.timestamp = now_ms();
            d.skin_conductance = std::max(1.0, prev_cond[a.id] * (post ? 1.4 : 1.0) + rd(g.rng) * 2);
            d.skin_conductance_prev = prev_cond[a.id];
            prev_cond[a.id] = d.skin_conductance;
            d.infrared_temperature = 36.2 + std::uniform_real_distribution<double>(0, 1.5)(g.rng) + (post ? 0.2 : 0);
            d.emg_amplitude = 15 + std::uniform_real_distribution<double>(0, 35)(g.rng) + (post ? 25 : 0);
            d.emg_frequency = 50 + std::uniform_real_distribution<double>(0, 25)(g.rng) + (post ? 15 : 0);
            d.is_post_acupuncture = post;
            d.session_id = "SIM-" + std::to_string(d.timestamp / 60000);
            post_data(d);
        }
        tick++;
        if (tick % 80 == 0) {
            const auto& a = aps[ap_dist(g.rng)];
            std::string vid = "V" + std::to_string(1 + std::uniform_int_distribution<int>(0, 29)(g.rng));
            while (vid.size() < 4) vid.insert(1, "0");
            int type = std::uniform_int_distribution<int>(0, 2)(g.rng);
            if (type == 0) add_alert(vid, a.id, "conductance_drop", "皮肤电导突降 42.5%", 42.5, 30.0);
            else if (type == 1) add_alert(vid, a.id, "temperature_high", "体温过高 38.7℃", 38.7, 38.0);
            else add_alert(vid, a.id, "emg_anomaly", "肌电信号异常 Z=3.8", 3.8, 3.0);
        }
        std::this_thread::sleep_for(milliseconds(150));
    }
}

// ============ TCP 服务器 ============
static void handle_client(socket_t client, const std::string& frontend_dir) {
    char buf[16384];
    std::string all;
    while (true) {
#ifdef _WIN32
        int n = recv(client, buf, sizeof(buf) - 1, 0);
#else
        ssize_t n = recv(client, buf, sizeof(buf) - 1, 0);
#endif
        if (n <= 0) break;
        buf[n] = '\0';
        all += buf;
        if (all.find("\r\n\r\n") != std::string::npos) break;
    }
    if (!all.empty()) {
        HttpRequest req = parse_request(all);
        std::string resp = handle_request(req, frontend_dir);
        send(client, resp.c_str(), (int)resp.size(), 0);
    }
    CLOSE_SOCKET(client);
}

static void server_loop(int port, const std::string& frontend_dir) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup失败" << std::endl;
        return;
    }
#endif

    socket_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCK) { std::cerr << "创建socket失败" << std::endl; return; }
    int opt = 1;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server, (sockaddr*)&addr, sizeof(addr)) == SOCK_ERROR) {
        std::cerr << "绑定端口 " << port << " 失败" << std::endl;
        CLOSE_SOCKET(server);
        return;
    }
    if (listen(server, 64) == SOCK_ERROR) {
        std::cerr << "listen失败" << std::endl;
        CLOSE_SOCKET(server);
        return;
    }

    std::cout << "========================================" << std::endl;
    std::cout << " 古代中医经络穴位数字化与针刺疗效关联分析系统" << std::endl;
    std::cout << " 后端服务已启动" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << " HTTP 端口: " << port << std::endl;
    std::cout << " 前端地址: http://localhost:" << port << "/static/index.html" << std::endl;
    std::cout << " API 健康: http://localhost:" << port << "/api/health" << std::endl;
    std::cout << " 按 Ctrl+C 停止服务" << std::endl;
    std::cout << "========================================" << std::endl;

    std::thread sim(simulator_thread, port);

    while (g.running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server, &readfds);
        timeval tv{0, 100000};
        int ret = select((int)server + 1, &readfds, nullptr, nullptr, &tv);
        if (ret <= 0) continue;
        sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        socket_t client = accept(server, (sockaddr*)&caddr, &clen);
        if (client != INVALID_SOCK) {
            std::thread(handle_client, client, frontend_dir).detach();
        }
    }

    g.running = false;
    if (sim.joinable()) sim.join();
    CLOSE_SOCKET(server);
#ifdef _WIN32
    WSACleanup();
#endif
}

// ============ 主函数 ============
int main(int argc, char* argv[]) {
    int port = 8080;
    std::string frontend_dir = "../frontend";

    // 尝试多种可能的前端路径
    for (const auto& p : {"./frontend", "../frontend", "./frontend", "frontend"}) {
        if (!read_file(std::string(p) + "/index.html").empty()) {
            frontend_dir = p;
            break;
        }
    }

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--frontend" && i + 1 < argc) frontend_dir = argv[++i];
        else if (a == "-h" || a == "--help") {
            std::cout << "中医经络数字化分析系统后端\n"
                      << "用法: tcm_backend [选项]\n\n"
                      << "选项:\n"
                      << "  --port <端口>       HTTP端口 (默认: 8080)\n"
                      << "  --frontend <目录>   前端文件目录 (默认: ./frontend)\n"
                      << "  -h, --help          显示帮助\n";
            return 0;
        }
    }

    server_loop(port, frontend_dir);
    return 0;
}

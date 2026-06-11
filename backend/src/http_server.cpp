#define CROW_ENABLE_SSL
#define CROW_MAIN
#include "http_server.h"
#include "crow.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>

namespace tcm {

struct HttpServer::Impl {
    crow::SimpleApp app;
    std::thread server_thread;
};

HttpServer::HttpServer()
    : db_manager_(std::make_unique<MongoDBManager>())
    , ble_receiver_(std::make_unique<BLEDataReceiver>())
    , rf_model_(std::make_unique<RandomForestModel>())
    , network_analyzer_(std::make_unique<MeridianNetworkAnalyzer>())
    , anomaly_detector_(std::make_unique<AnomalyDetector>())
    , dingtalk_notifier_(std::make_unique<DingTalkNotifier>())
    , ws_manager_(std::make_unique<WebSocketManager>())
    , data_processor_(std::make_unique<DataProcessor>())
    , running_(false)
    , port_(8080) {
}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start(int http_port, const std::string& mongodb_uri, const std::string& db_name) {
    port_ = http_port;

    std::cout << "[初始化] 连接MongoDB: " << mongodb_uri << "/" << db_name << std::endl;
    if (!db_manager_->initialize(mongodb_uri, db_name)) {
        std::cerr << "[错误] MongoDB连接失败" << std::endl;
        return false;
    }
    db_manager_->ensure_indexes();

    std::cout << "[初始化] 加载随机森林预测模型" << std::endl;
    rf_model_->load_or_initialize("./rf_model.bin");

    std::cout << "[初始化] 启动经络网络分析器" << std::endl;
    network_analyzer_->initialize();

    std::cout << "[初始化] 配置异常检测器" << std::endl;
    anomaly_detector_->set_alert_callback([this](const Alert& alert) {
        on_alert_generated(alert);
    });

    std::cout << "[初始化] 配置钉钉通知" << std::endl;
    dingtalk_notifier_->initialize("https://oapi.dingtalk.com/robot/send?access_token=YOUR_TOKEN", "YOUR_SECRET");

    std::cout << "[初始化] 配置数据处理器" << std::endl;
    data_processor_->set_efficacy_callback([this](const SensorData& data, const EfficacyRecord& eff, const PredictionResult& pred) {
        on_data_processed(data, eff, pred);
    });

    ble_receiver_->set_data_callback([this](const SensorData& data) {
        on_sensor_data_received(data);
    });
    ble_receiver_->start(8081);

    setup_routes();

    running_ = true;

    std::thread([this, http_port]() {
        try {
            CROW_ROUTE(dynamic_cast<crow::SimpleApp*>((void*)nullptr), "/test");
        } catch (...) {}
    }).detach();

    return true;
}

void HttpServer::stop() {
    if (!running_.exchange(false)) return;
    ble_receiver_->stop();
    db_manager_->shutdown();
    running_ = false;
}

bool HttpServer::is_running() const {
    return running_.load();
}

void HttpServer::setup_routes() {
    setup_rest_api();
    setup_websocket_routes();
}

void HttpServer::setup_rest_api() {
}

void HttpServer::setup_websocket_routes() {
}

void HttpServer::on_sensor_data_received(const SensorData& data) {
    db_manager_->insert_sensor_data(data);
    anomaly_detector_->process_sensor_data(data);
    data_processor_->process_sensor_data(data);
    ws_manager_->broadcast_sensor_data(data);
}

void HttpServer::on_alert_generated(const Alert& alert) {
    db_manager_->insert_alert(alert);
    ws_manager_->broadcast_alert(alert);
    dingtalk_notifier_->send_alert(alert);
}

void HttpServer::on_data_processed(const SensorData& data, const EfficacyRecord& eff, const PredictionResult& pred) {
    db_manager_->insert_efficacy_record(eff);
    db_manager_->insert_prediction(pred);
    ws_manager_->broadcast_prediction(pred);
    ws_manager_->broadcast_efficacy_record(eff);
}

} // namespace tcm

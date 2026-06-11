#pragma once
#include "mongodb_manager.h"
#include "ble_data_receiver.h"
#include "random_forest_model.h"
#include "meridian_network_analyzer.h"
#include "anomaly_detector.h"
#include "dingtalk_notifier.h"
#include "websocket_manager.h"
#include "data_processor.h"
#include <string>
#include <memory>
#include <atomic>

namespace tcm {

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    bool start(
        int http_port = 8080,
        const std::string& mongodb_uri = "mongodb://localhost:27017",
        const std::string& db_name = "tcm_acupuncture"
    );

    void stop();

    bool is_running() const;

private:
    void setup_routes();
    void setup_rest_api();
    void setup_websocket_routes();

    void on_sensor_data_received(const SensorData& data);
    void on_alert_generated(const Alert& alert);
    void on_data_processed(const SensorData& data, const EfficacyRecord& eff, const PredictionResult& pred);

    std::unique_ptr<MongoDBManager> db_manager_;
    std::unique_ptr<BLEDataReceiver> ble_receiver_;
    std::unique_ptr<RandomForestModel> rf_model_;
    std::unique_ptr<MeridianNetworkAnalyzer> network_analyzer_;
    std::unique_ptr<AnomalyDetector> anomaly_detector_;
    std::unique_ptr<DingTalkNotifier> dingtalk_notifier_;
    std::unique_ptr<WebSocketManager> ws_manager_;
    std::unique_ptr<DataProcessor> data_processor_;

    std::atomic<bool> running_;
    int port_;
};

} // namespace tcm

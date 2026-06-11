#include "mongodb_manager.h"
#include <iostream>
#include <sstream>
#include <algorithm>

#if __has_include(<mongocxx/client.hpp>)
#define HAS_MONGOCXX 1
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <mongocxx/database.hpp>
#include <mongocxx/collection.hpp>
#include <mongocxx/exception/exception.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/stream/array.hpp>
#include <bsoncxx/builder/stream/helpers.hpp>
#include <mongocxx/stdx.hpp>
#else
#define HAS_MONGOCXX 0
#endif

namespace tcm {

struct MongoDBManager::Impl {
#if HAS_MONGOCXX
    std::unique_ptr<mongocxx::instance> instance;
    std::unique_ptr<mongocxx::client> client;
    mongocxx::database db;
#endif
    std::string db_name;
    bool connected;
};

MongoDBManager::MongoDBManager()
    : impl_(std::make_unique<Impl>())
    , initialized_(false) {
    impl_->connected = false;
}

MongoDBManager::~MongoDBManager() {
    shutdown();
}

MongoDBManager& MongoDBManager::instance() {
    static MongoDBManager inst;
    return inst;
}

bool MongoDBManager::initialize(const std::string& uri, const std::string& db_name) {
    std::lock_guard<std::mutex> lk(mutex_);
    impl_->db_name = db_name;

#if HAS_MONGOCXX
    try {
        impl_->instance = std::make_unique<mongocxx::instance>();
        mongocxx::uri muri(uri);
        impl_->client = std::make_unique<mongocxx::client>(muri);
        impl_->db = (*impl_->client)[db_name];
        impl_->connected = true;
        initialized_ = true;
        std::cout << "[MongoDB] 连接成功: " << uri << "/" << db_name << std::endl;

        auto admin_db = (*impl_->client)["admin"];
        auto ping_cmd = bsoncxx::builder::stream::document{} << "ping" << 1
                         << bsoncxx::builder::stream::finalize;
        admin_db.run_command(ping_cmd.view());
        std::cout << "[MongoDB] Ping成功" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[MongoDB] 连接失败: " << e.what() << "，使用内存模式" << std::endl;
    }
#endif

    std::cout << "[MongoDB] 使用内存模式 (未找到mongocxx)" << std::endl;
    initialized_ = true;
    return true;
}

void MongoDBManager::shutdown() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!initialized_) return;
#if HAS_MONGOCXX
    impl_->client.reset();
    impl_->instance.reset();
#endif
    impl_->connected = false;
    initialized_ = false;
}

bool MongoDBManager::insert_sensor_data(const SensorData& data) {
    if (!initialized_) return false;
#if HAS_MONGOCXX
    if (!impl_->connected) return false;
    try {
        auto coll = impl_->db["sensor_data"];
        bsoncxx::builder::stream::document doc;
        doc << "volunteer_id" << data.volunteer_id
            << "acupoint_id" << data.acupoint_id
            << "meridian_id" << data.meridian_id
            << "timestamp" << (int64_t)data.timestamp
            << "skin_conductance" << data.skin_conductance
            << "skin_conductance_prev" << data.skin_conductance_prev
            << "infrared_temperature" << data.infrared_temperature
            << "emg_amplitude" << data.emg_amplitude
            << "emg_frequency" << data.emg_frequency
            << "is_post_acupuncture" << data.is_post_acupuncture
            << "session_id" << data.session_id;
        coll.insert_one(doc << bsoncxx::builder::stream::finalize);
        return true;
    } catch (...) {}
#endif
    return true;
}

bool MongoDBManager::insert_sensor_data_batch(const std::vector<SensorData>& batch) {
    for (const auto& d : batch) insert_sensor_data(d);
    return true;
}

std::vector<SensorData> MongoDBManager::query_sensor_data(
    const std::string& volunteer_id,
    const std::string& acupoint_id,
    uint64_t start_time, uint64_t end_time, int limit) {
    std::vector<SensorData> result;
#if HAS_MONGOCXX
    if (!impl_->connected) return result;
    try {
        auto coll = impl_->db["sensor_data"];
        bsoncxx::builder::stream::document query;
        query << "timestamp" << bsoncxx::builder::stream::open_document
              << "$gte" << (int64_t)start_time
              << "$lte" << (int64_t)end_time
              << bsoncxx::builder::stream::close_document;
        if (!volunteer_id.empty()) query << "volunteer_id" << volunteer_id;
        if (!acupoint_id.empty()) query << "acupoint_id" << acupoint_id;

        mongocxx::options::find opts;
        opts.sort(bsoncxx::builder::stream::document{} << "timestamp" << 1 << bsoncxx::builder::stream::finalize);
        opts.limit(limit);

        auto cursor = coll.find(query << bsoncxx::builder::stream::finalize, opts);
        for (auto&& doc : cursor) {
            SensorData d;
            try {
                d.volunteer_id = std::string(doc["volunteer_id"].get_utf8().value);
                d.acupoint_id = std::string(doc["acupoint_id"].get_utf8().value);
                if (doc.find("meridian_id") != doc.end())
                    d.meridian_id = std::string(doc["meridian_id"].get_utf8().value);
                d.timestamp = (uint64_t)doc["timestamp"].get_int64().value;
                d.skin_conductance = doc["skin_conductance"].get_double().value;
                d.skin_conductance_prev = doc["skin_conductance_prev"].get_double().value;
                d.infrared_temperature = doc["infrared_temperature"].get_double().value;
                d.emg_amplitude = doc["emg_amplitude"].get_double().value;
                d.emg_frequency = doc["emg_frequency"].get_double().value;
                d.is_post_acupuncture = doc["is_post_acupuncture"].get_bool().value;
                if (doc.find("session_id") != doc.end())
                    d.session_id = std::string(doc["session_id"].get_utf8().value);
                result.push_back(d);
            } catch (...) {}
        }
    } catch (...) {}
#endif
    return result;
}

bool MongoDBManager::insert_efficacy_record(const EfficacyRecord& record) {
#if HAS_MONGOCXX
    if (!impl_->connected) return false;
    try {
        auto coll = impl_->db["efficacy_records"];
        bsoncxx::builder::stream::document doc;
        doc << "volunteer_id" << record.volunteer_id
            << "session_id" << record.session_id
            << "acupoint_id" << record.acupoint_id
            << "timestamp" << (int64_t)record.timestamp
            << "deqi_intensity" << record.deqi_intensity
            << "pain_relief_rate" << record.pain_relief_rate
            << "efficacy_text" << record.efficacy_text;
        auto arr = bsoncxx::builder::stream::array{};
        for (const auto& s : record.symptoms) arr << s;
        doc << "symptoms" << arr;
        doc << "practitioner_notes" << record.practitioner_notes;
        coll.insert_one(doc << bsoncxx::builder::stream::finalize);
    } catch (...) { return false; }
#endif
    return true;
}

std::vector<EfficacyRecord> MongoDBManager::query_efficacy_records(
    const std::string& volunteer_id, uint64_t start_time, uint64_t end_time) {
    std::vector<EfficacyRecord> result;
#if HAS_MONGOCXX
    if (!impl_->connected) return result;
    try {
        auto coll = impl_->db["efficacy_records"];
        bsoncxx::builder::stream::document q;
        q << "timestamp" << bsoncxx::builder::stream::open_document
          << "$gte" << (int64_t)start_time << "$lte" << (int64_t)end_time
          << bsoncxx::builder::stream::close_document;
        if (!volunteer_id.empty()) q << "volunteer_id" << volunteer_id;
        auto cursor = coll.find(q << bsoncxx::builder::stream::finalize);
        for (auto&& doc : cursor) {
            EfficacyRecord r;
            try {
                r.volunteer_id = std::string(doc["volunteer_id"].get_utf8().value);
                r.session_id = std::string(doc["session_id"].get_utf8().value);
                r.acupoint_id = std::string(doc["acupoint_id"].get_utf8().value);
                r.timestamp = (uint64_t)doc["timestamp"].get_int64().value;
                r.deqi_intensity = doc["deqi_intensity"].get_double().value;
                r.pain_relief_rate = doc["pain_relief_rate"].get_double().value;
                r.efficacy_text = std::string(doc["efficacy_text"].get_utf8().value);
                result.push_back(r);
            } catch (...) {}
        }
    } catch (...) {}
#endif
    return result;
}

bool MongoDBManager::insert_alert(const Alert& alert) {
#if HAS_MONGOCXX
    if (!impl_->connected) return false;
    try {
        auto coll = impl_->db["alerts"];
        bsoncxx::builder::stream::document doc;
        doc << "id" << alert.id
            << "timestamp" << (int64_t)alert.timestamp
            << "volunteer_id" << alert.volunteer_id
            << "acupoint_id" << alert.acupoint_id
            << "alert_type" << alert.alert_type
            << "message" << alert.message
            << "value" << alert.value
            << "threshold" << alert.threshold
            << "acknowledged" << alert.acknowledged;
        coll.insert_one(doc << bsoncxx::builder::stream::finalize);
    } catch (...) { return false; }
#endif
    return true;
}

std::vector<Alert> MongoDBManager::query_alerts(uint64_t start_time, uint64_t end_time, bool ack_only) {
    std::vector<Alert> result;
#if HAS_MONGOCXX
    if (!impl_->connected) return result;
    try {
        auto coll = impl_->db["alerts"];
        bsoncxx::builder::stream::document q;
        q << "timestamp" << bsoncxx::builder::stream::open_document
          << "$gte" << (int64_t)start_time << "$lte" << (int64_t)end_time
          << bsoncxx::builder::stream::close_document;
        if (ack_only) q << "acknowledged" << true;
        auto cursor = coll.find(q << bsoncxx::builder::stream::finalize);
        for (auto&& doc : cursor) {
            Alert a;
            try {
                a.id = std::string(doc["id"].get_utf8().value);
                a.timestamp = (uint64_t)doc["timestamp"].get_int64().value;
                a.volunteer_id = std::string(doc["volunteer_id"].get_utf8().value);
                a.acupoint_id = std::string(doc["acupoint_id"].get_utf8().value);
                a.alert_type = std::string(doc["alert_type"].get_utf8().value);
                a.message = std::string(doc["message"].get_utf8().value);
                a.value = doc["value"].get_double().value;
                a.threshold = doc["threshold"].get_double().value;
                a.acknowledged = doc["acknowledged"].get_bool().value;
                result.push_back(a);
            } catch (...) {}
        }
    } catch (...) {}
#endif
    return result;
}

bool MongoDBManager::acknowledge_alert(const std::string& alert_id) {
#if HAS_MONGOCXX
    if (!impl_->connected) return false;
    try {
        auto coll = impl_->db["alerts"];
        auto filter = bsoncxx::builder::stream::document{} << "id" << alert_id
                     << bsoncxx::builder::stream::finalize;
        auto update = bsoncxx::builder::stream::document{}
                     << "$set" << bsoncxx::builder::stream::open_document
                     << "acknowledged" << true
                     << bsoncxx::builder::stream::close_document
                     << bsoncxx::builder::stream::finalize;
        coll.update_one(filter.view(), update.view());
        return true;
    } catch (...) {}
#endif
    return false;
}

bool MongoDBManager::insert_prediction(const PredictionResult& pred) {
#if HAS_MONGOCXX
    if (!impl_->connected) return false;
    try {
        auto coll = impl_->db["predictions"];
        bsoncxx::builder::stream::document doc;
        doc << "volunteer_id" << pred.volunteer_id
            << "session_id" << pred.session_id
            << "timestamp" << (int64_t)pred.timestamp
            << "predicted_deqi" << pred.predicted_deqi
            << "predicted_pain_relief" << pred.predicted_pain_relief
            << "confidence" << pred.confidence;
        auto arr = bsoncxx::builder::stream::array{};
        for (const auto& f : pred.feature_importance) arr << f;
        doc << "feature_importance" << arr;
        coll.insert_one(doc << bsoncxx::builder::stream::finalize);
    } catch (...) { return false; }
#endif
    return true;
}

std::vector<PredictionResult> MongoDBManager::query_predictions(
    const std::string& volunteer_id, const std::string& session_id) {
    std::vector<PredictionResult> result;
#if HAS_MONGOCXX
    if (!impl_->connected) return result;
    try {
        auto coll = impl_->db["predictions"];
        bsoncxx::builder::stream::document q;
        if (!volunteer_id.empty()) q << "volunteer_id" << volunteer_id;
        if (!session_id.empty()) q << "session_id" << session_id;
        auto cursor = coll.find(q << bsoncxx::builder::stream::finalize);
        for (auto&& doc : cursor) {
            PredictionResult p;
            try {
                p.volunteer_id = std::string(doc["volunteer_id"].get_utf8().value);
                p.session_id = std::string(doc["session_id"].get_utf8().value);
                p.timestamp = (uint64_t)doc["timestamp"].get_int64().value;
                p.predicted_deqi = doc["predicted_deqi"].get_double().value;
                p.predicted_pain_relief = doc["predicted_pain_relief"].get_double().value;
                p.confidence = doc["confidence"].get_double().value;
                result.push_back(p);
            } catch (...) {}
        }
    } catch (...) {}
#endif
    return result;
}

std::vector<AcupointInfo> get_default_acupoints();
std::vector<MeridianInfo> get_default_meridians();

std::vector<AcupointInfo> MongoDBManager::get_all_acupoints() {
    std::vector<AcupointInfo> result;
#if HAS_MONGOCXX
    if (impl_->connected) {
        try {
            auto coll = impl_->db["acupoints"];
            auto cursor = coll.find({});
            for (auto&& doc : cursor) {
                AcupointInfo a;
                try {
                    a.id = std::string(doc["id"].get_utf8().value);
                    a.name = std::string(doc["name"].get_utf8().value);
                    a.pinyin = std::string(doc["pinyin"].get_utf8().value);
                    a.meridian_id = std::string(doc["meridian_id"].get_utf8().value);
                    a.x = doc["x"].get_double().value;
                    a.y = doc["y"].get_double().value;
                    a.z = doc["z"].get_double().value;
                    a.description = std::string(doc["description"].get_utf8().value);
                    result.push_back(a);
                } catch (...) {}
            }
            if (!result.empty()) return result;
        } catch (...) {}
    }
#endif
    return get_default_acupoints();
}

std::vector<MeridianInfo> MongoDBManager::get_all_meridians() {
    std::vector<MeridianInfo> result;
#if HAS_MONGOCXX
    if (impl_->connected) {
        try {
            auto coll = impl_->db["meridians"];
            auto cursor = coll.find({});
            for (auto&& doc : cursor) {
                MeridianInfo m;
                try {
                    m.id = std::string(doc["id"].get_utf8().value);
                    m.name = std::string(doc["name"].get_utf8().value);
                    m.pinyin = std::string(doc["pinyin"].get_utf8().value);
                    m.element = std::string(doc["element"].get_utf8().value);
                    result.push_back(m);
                } catch (...) {}
            }
            if (!result.empty()) return result;
        } catch (...) {}
    }
#endif
    return get_default_meridians();
}

AcupointInfo MongoDBManager::get_acupoint(const std::string& acupoint_id) {
    auto all = get_all_acupoints();
    for (const auto& a : all) if (a.id == acupoint_id) return a;
    return AcupointInfo{};
}

MeridianInfo MongoDBManager::get_meridian(const std::string& meridian_id) {
    auto all = get_all_meridians();
    for (const auto& m : all) if (m.id == meridian_id) return m;
    return MeridianInfo{};
}

bool MongoDBManager::ensure_indexes() {
#if HAS_MONGOCXX
    if (!impl_->connected) return false;
    try {
        auto sensor = impl_->db["sensor_data"];
        sensor.create_index(bsoncxx::builder::stream::document{}
            << "volunteer_id" << 1 << "acupoint_id" << 1 << "timestamp" << 1
            << bsoncxx::builder::stream::finalize);
        auto alerts = impl_->db["alerts"];
        alerts.create_index(bsoncxx::builder::stream::document{}
            << "timestamp" << -1 << bsoncxx::builder::stream::finalize);
        return true;
    } catch (...) {}
#endif
    return false;
}

} // namespace tcm

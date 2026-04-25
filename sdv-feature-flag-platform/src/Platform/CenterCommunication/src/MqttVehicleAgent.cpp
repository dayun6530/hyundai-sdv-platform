#include "CenterCommunication/MqttVehicleAgent.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <utility>

#include <mqtt/async_client.h>
#include "CenterCommunication/MqttProtocolV1.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace sdv::platform::centercommunication {
namespace {

constexpr const char* kLogTag = "MqttVehicleAgent";

class MissingFeatureController final : public IFeatureController {
public:
    void stopFeature(const std::string& logicalName) override {
        (void)logicalName;
        throw std::runtime_error("FeatureController is not configured (expected EventBusFeatureController)");
    }

    void deployFeatureBinary(const std::string& logicalName,
                             const std::string& version,
                             const std::string& payloadUrl) override {
        (void)logicalName;
        (void)version;
        (void)payloadUrl;
        throw std::runtime_error("FeatureController is not configured (expected EventBusFeatureController)");
    }
};

static mqtt::connect_options makeConnectOptions(const MqttVehicleAgent::Config& cfg) {
    mqtt::connect_options opts;
    opts.set_clean_start(cfg.cleanStart);
    opts.set_keep_alive_interval(std::chrono::seconds(cfg.keepAliveSeconds));
    opts.set_automatic_reconnect(std::chrono::seconds(cfg.reconnectMinSeconds),
                                 std::chrono::seconds(cfg.reconnectMaxSeconds));
    return opts;
}

static int clampQos(int qos) {
    if (qos < 0) return 0;
    if (qos > 2) return 2;
    return qos;
}

}  // namespace

class MqttVehicleAgent::Callback final : public virtual mqtt::callback,
                                        public virtual mqtt::iaction_listener {
public:
    explicit Callback(MqttVehicleAgent& owner) : owner_(owner) {}

    void connected(const std::string& cause) override {
        spdlog::info("[{}] connected (cause='{}')", kLogTag, cause);
        try {
            owner_.ensureConnectedAndSubscribed();
        } catch (const std::exception& e) {
            spdlog::error("[{}] post-connect subscribe failed: {}", kLogTag, e.what());
        }
    }

    void connection_lost(const std::string& cause) override {
        spdlog::warn("[{}] connection lost (cause='{}')", kLogTag, cause);
    }

    void message_arrived(mqtt::const_message_ptr msg) override {
        owner_.onMqttMessage(std::move(msg));
    }

    void delivery_complete(mqtt::delivery_token_ptr tok) override {
        if (!tok) return;
        spdlog::debug("[{}] delivery complete: msgId={}", kLogTag, tok->get_message_id());
    }

    void on_failure(const mqtt::token& tok) override {
        spdlog::error("[{}] action failed: type={} rc={}",
                      kLogTag,
                      tok.get_type(),
                      tok.get_return_code());
    }

    void on_success(const mqtt::token& tok) override {
        spdlog::debug("[{}] action success: type={}", kLogTag, tok.get_type());
    }

private:
    MqttVehicleAgent& owner_;
};

MqttVehicleAgent& MqttVehicleAgent::instance() {
    static MqttVehicleAgent inst;
    return inst;
}

MqttVehicleAgent::MqttVehicleAgent()
    : featureController_(std::make_shared<MissingFeatureController>()) {}

MqttVehicleAgent::~MqttVehicleAgent() {
    stop();
}

void MqttVehicleAgent::configure(Config cfg) {
    cfg.qos = clampQos(cfg.qos);
    if (cfg.qos < 1) {
        throw std::invalid_argument("MqttVehicleAgent requires QoS >= 1");
    }

    std::lock_guard<std::mutex> lock(mu_);
    cfg_ = std::move(cfg);
}

void MqttVehicleAgent::setFeatureController(std::shared_ptr<IFeatureController> controller) {
    if (!controller) {
        throw std::invalid_argument("FeatureController must not be null");
    }
    std::lock_guard<std::mutex> lock(mu_);
    featureController_ = std::move(controller);
}

bool MqttVehicleAgent::isRunning() const {
    return running_.load(std::memory_order_relaxed);
}

void MqttVehicleAgent::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        spdlog::warn("[{}] start ignored: already running", kLogTag);
        return;
    }

    stopRequested_.store(false);

    Config cfgCopy;
    {
        std::lock_guard<std::mutex> lock(mu_);
        cfgCopy = cfg_;
    }

    spdlog::info("[{}] starting: brokerUri={} clientId={} subTopic={} pubTopic={} qos={}",
                 kLogTag,
                 cfgCopy.brokerUri,
                 cfgCopy.clientId,
                 cfgCopy.subscribeTopic,
                 cfgCopy.publishTopic,
                 cfgCopy.qos);

    client_ = std::make_unique<mqtt::async_client>(cfgCopy.brokerUri, cfgCopy.clientId);
    client_->start_consuming();

    loopThread_ = std::thread([this]() { this->eventLoop(); });
}

void MqttVehicleAgent::stop() {
    if (!running_.load()) return;

    stopRequested_.store(true);
    cv_.notify_all();

    if (loopThread_.joinable()) {
        loopThread_.join();
    }

    try {
        if (client_ && client_->is_connected()) {
            spdlog::info("[{}] disconnecting", kLogTag);
            client_->disconnect()->wait();
        }
    } catch (const std::exception& e) {
        spdlog::warn("[{}] disconnect error (ignored): {}", kLogTag, e.what());
    }

    if (client_) {
        try {
            client_->stop_consuming();
        } catch (...) {
        }
    }
    callback_.reset();
    client_.reset();

    running_.store(false);
    spdlog::info("[{}] stopped", kLogTag);
}

void MqttVehicleAgent::eventLoop() {
    try {
        ensureConnectedAndSubscribed();
    } catch (const std::exception& e) {
        spdlog::error("[{}] initial connect/subscribe failed: {}", kLogTag, e.what());
    }

    while (!stopRequested_.load()) {
        if (!client_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        if (!client_->is_connected()) {
            try {
                ensureConnectedAndSubscribed();
            } catch (const std::exception& e) {
                spdlog::warn("[{}] reconnect/subscribe retry failed: {}", kLogTag, e.what());
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
        }

        // Consume messages in this dedicated event-loop thread.
        auto msg = client_->try_consume_message_for(std::chrono::milliseconds(200));
        if (!msg) continue;
        try {
            handleCommandJson(msg->get_topic(), msg->to_string());
        } catch (const std::exception& e) {
            spdlog::error("[{}] message handling error: {}", kLogTag, e.what());
        }
    }
}

void MqttVehicleAgent::ensureConnectedAndSubscribed() {
    Config cfgCopy;
    {
        std::lock_guard<std::mutex> lock(mu_);
        cfgCopy = cfg_;
    }

    if (!client_) {
        throw std::runtime_error("MQTT client not initialized");
    }

    if (!client_->is_connected()) {
        spdlog::info("[{}] connecting to broker {}", kLogTag, cfgCopy.brokerUri);
        auto opts = makeConnectOptions(cfgCopy);
        client_->connect(opts)->wait();
        spdlog::info("[{}] connect complete", kLogTag);
    }

    spdlog::info("[{}] subscribing: topic='{}' qos={}", kLogTag, cfgCopy.subscribeTopic, cfgCopy.qos);
    client_->subscribe(cfgCopy.subscribeTopic, cfgCopy.qos)->wait();
}

void MqttVehicleAgent::onMqttMessage(mqtt::const_message_ptr msg) {
    if (!msg) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        inbox_.push(std::move(msg));
    }
    cv_.notify_one();
}

void MqttVehicleAgent::handleCommandJson(const std::string& topic, const std::string& payload) {
    spdlog::info("[{}] rx: topic='{}' payload='{}'", kLogTag, topic, payload);

    // --- Protocol v1 (envelope) path ---
    {
        const auto decoded = mqttproto::decodeMessageV1(payload);
        if (decoded.message.has_value()) {
            const mqttproto::MessageV1& m = *decoded.message;
            const std::string requestId = m.header.msgId;

            if (m.command == mqttproto::kCommandKillSwitch) {
                // Execute and ACK using telemetry format (correlatable by request_id).
                std::shared_ptr<IFeatureController> controller;
                {
                    std::lock_guard<std::mutex> lock(mu_);
                    controller = featureController_;
                }

                try {
                    controller->stopFeature(m.payload.featureName);
                    publishTelemetryAck(requestId,
                                        m.command,
                                        m.payload.featureName,
                                        std::optional<std::string>(m.payload.version),
                                        "KILL_ACK",
                                        "Feature stopped");
                } catch (const std::exception& e) {
                    publishTelemetryAck(requestId,
                                        m.command,
                                        m.payload.featureName,
                                        std::optional<std::string>(m.payload.version),
                                        "ERROR",
                                        std::string("KILL_FAILED: ") + e.what());
                }
                return;
            }

            if (m.command == mqttproto::kCommandDeployFeature) {
                if (m.payload.featureName.empty()) {
                    publishTelemetryAck(requestId,
                                        m.command,
                                        "",
                                        std::optional<std::string>(m.payload.version),
                                        "ERROR",
                                        "MISSING_FIELD: payload.feature_name");
                    return;
                }
                if (m.payload.version.empty()) {
                    publishTelemetryAck(requestId,
                                        m.command,
                                        m.payload.featureName,
                                        std::nullopt,
                                        "ERROR",
                                        "MISSING_FIELD: payload.version");
                    return;
                }
                if (m.payload.binaryPath.empty()) {
                    // Shadow-run may be requested without an immediate binary path (info-only handshake).
                    if (m.payload.action == "SHADOW_RUN") {
                        const std::string current = m.payload.currentFeature.value_or("-");
                        publishTelemetryAck(requestId,
                                            m.command,
                                            m.payload.featureName,
                                            std::optional<std::string>(m.payload.version),
                                            "SHADOW_ACK",
                                            "Shadow run request accepted (current_feature=" + current + ")");
                        return;
                    }

                    publishTelemetryAck(requestId,
                                        m.command,
                                        m.payload.featureName,
                                        std::optional<std::string>(m.payload.version),
                                        "ERROR",
                                        "MISSING_FIELD: payload.binary_path");
                    return;
                }

                // Treat binary_path as payload_url equivalent (download/install location).
                try {
                    handleDeploy(m.payload.featureName, m.payload.version, m.payload.binaryPath);
                    // handleDeploy already publishes legacy ack; also send telemetry ack for server correlation.
                    publishTelemetryAck(requestId,
                                        m.command,
                                        m.payload.featureName,
                                        std::optional<std::string>(m.payload.version),
                                        "DEPLOY_SUCCESS",
                                        "Binary updated");
                } catch (const std::exception& e) {
                    publishTelemetryAck(requestId,
                                        m.command,
                                        m.payload.featureName,
                                        std::optional<std::string>(m.payload.version),
                                        "ERROR",
                                        std::string("DEPLOY_FAILED: ") + e.what());
                }
                return;
            }

            publishTelemetryAck(requestId,
                                m.command,
                                m.payload.featureName,
                                std::optional<std::string>(m.payload.version),
                                "ERROR",
                                "UNKNOWN_COMMAND");
            return;
        }
    }

    // --- Legacy path (flat JSON) ---
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(payload);
    } catch (const std::exception& e) {
        publishAck("UNKNOWN", "", std::nullopt, "ERROR", std::string("JSON_PARSE_ERROR: ") + e.what());
        return;
    }

    const std::string command = j.value("command", "");
    const std::string targetFeature = j.value("target_feature", "");
    const std::string versionStr = j.value("version", "");
    const std::string payloadUrl = j.value("payload_url", "");

    std::optional<std::string> version;
    if (!versionStr.empty()) version = versionStr;

    if (command.empty()) {
        publishAck("UNKNOWN", targetFeature, version, "ERROR", "MISSING_FIELD: command");
        return;
    }

    if (targetFeature.empty()) {
        publishAck(command, targetFeature, version, "ERROR", "MISSING_FIELD: target_feature");
        return;
    }

    if (command == "KillSwitch") {
        handleKillSwitch(targetFeature, version);
        return;
    }

    if (command == "Deploy") {
        if (!version || version->empty()) {
            publishAck(command, targetFeature, version, "ERROR", "MISSING_FIELD: version");
            return;
        }
        if (payloadUrl.empty()) {
            publishAck(command, targetFeature, version, "ERROR", "MISSING_FIELD: payload_url");
            return;
        }
        handleDeploy(targetFeature, *version, payloadUrl);
        return;
    }

    publishAck(command, targetFeature, version, "ERROR", "UNKNOWN_COMMAND");
}

void MqttVehicleAgent::handleKillSwitch(const std::string& targetFeature,
                                       const std::optional<std::string>& version) {
    std::shared_ptr<IFeatureController> controller;
    {
        std::lock_guard<std::mutex> lock(mu_);
        controller = featureController_;
    }

    spdlog::warn("[{}] KillSwitch: target_feature={} version={}",
                 kLogTag,
                 targetFeature,
                 version.value_or("-"));

    try {
        controller->stopFeature(targetFeature);
        publishAck("KillSwitch", targetFeature, version, "KILL_ACK", "Feature stopped");
    } catch (const std::exception& e) {
        publishAck("KillSwitch", targetFeature, version, "ERROR", std::string("KILL_FAILED: ") + e.what());
    }
}

void MqttVehicleAgent::handleDeploy(const std::string& targetFeature,
                                   const std::string& version,
                                   const std::string& payloadUrl) {
    std::shared_ptr<IFeatureController> controller;
    {
        std::lock_guard<std::mutex> lock(mu_);
        controller = featureController_;
    }

    spdlog::info("[{}] Deploy: target_feature={} version={} payload_url={}",
                 kLogTag,
                 targetFeature,
                 version,
                 payloadUrl);

    try {
        controller->deployFeatureBinary(targetFeature, version, payloadUrl);

        spdlog::info("[{}] simulating binary update...", kLogTag);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        spdlog::info("[{}] simulation done", kLogTag);

        publishAck("Deploy",
                   targetFeature,
                   std::optional<std::string>(version),
                   "DEPLOY_SUCCESS",
                   "Binary updated");
    } catch (const std::exception& e) {
        publishAck("Deploy",
                   targetFeature,
                   std::optional<std::string>(version),
                   "ERROR",
                   std::string("DEPLOY_FAILED: ") + e.what());
    }
}

void MqttVehicleAgent::publishAck(const std::string& command,
                                 const std::string& targetFeature,
                                 const std::optional<std::string>& version,
                                 const std::string& status,
                                 const std::string& detail) {
    if (!client_) return;

    Config cfgCopy;
    {
        std::lock_guard<std::mutex> lock(mu_);
        cfgCopy = cfg_;
    }

    nlohmann::json out = {
        {"vehicle_id", "001"},
        {"command", command},
        {"target_feature", targetFeature},
        {"status", status},
        {"detail", detail},
        {"ts_ms",
         std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
             .count()},
    };
    if (version && !version->empty()) out["version"] = *version;

    const std::string payload = out.dump();

    try {
        auto msg = mqtt::make_message(cfgCopy.publishTopic, payload);
        msg->set_qos(cfgCopy.qos);
        msg->set_retained(false);

        if (!client_->is_connected()) {
            spdlog::warn("[{}] publish skipped (not connected): {}", kLogTag, payload);
            return;
        }

        client_->publish(msg);
        spdlog::info("[{}] tx ack: topic='{}' payload='{}'",
                     kLogTag,
                     cfgCopy.publishTopic,
                     payload);
    } catch (const std::exception& e) {
        spdlog::error("[{}] publish error: {}", kLogTag, e.what());
    }
}

void MqttVehicleAgent::publishTelemetryAck(const std::string& requestId,
                                          const std::string& command,
                                          const std::string& targetFeature,
                                          const std::optional<std::string>& version,
                                          const std::string& status,
                                          const std::string& detail) {
    if (!client_) return;

    Config cfgCopy;
    {
        std::lock_guard<std::mutex> lock(mu_);
        cfgCopy = cfg_;
    }

    nlohmann::json out = {
        {"vehicle_id", "001"},
        {"request_id", requestId},
        {"command", command},
        {"target_feature", targetFeature},
        {"status", status},
        {"detail", detail},
        {"ts_ms",
         std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
             .count()},
    };
    if (version && !version->empty()) out["version"] = *version;

    const std::string payload = out.dump();
    try {
        auto msg = mqtt::make_message(cfgCopy.publishTopic, payload);
        msg->set_qos(cfgCopy.qos);
        msg->set_retained(false);

        if (!client_->is_connected()) {
            spdlog::warn("[{}] telemetry ack skipped (not connected): {}", kLogTag, payload);
            return;
        }
        client_->publish(msg);
        spdlog::info("[{}] tx telemetry: topic='{}' payload='{}'",
                     kLogTag,
                     cfgCopy.publishTopic,
                     payload);
    } catch (const std::exception& e) {
        spdlog::error("[{}] telemetry publish error: {}", kLogTag, e.what());
    }
}

}  // namespace sdv::platform::centercommunication


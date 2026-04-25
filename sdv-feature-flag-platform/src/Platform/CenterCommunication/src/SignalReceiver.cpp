#include "CenterCommunication/SignalReceiver.h"

#include <iostream>
#include <thread>
#include <signal.h>
#include <cstdlib>
#include <chrono>

#include <mqtt/async_client.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace sdv::platform::centercommunication {

namespace {

static std::string envOr(const char* name, const std::string& fallback) {
    if (const char* v = std::getenv(name); v != nullptr && *v != '\0') {
        return std::string(v);
    }
    return fallback;
}

}  // namespace

SignalReceiver::SignalReceiver(const std::string& centerUrl,
                               const std::string& signalFilePath)
    : Signal(centerUrl, signalFilePath) {}

void SignalReceiver::start() {
    running_ = true;
    stopRequested_.store(false);
    std::cout << "[SignalReceiver] Started. Center: " << centerUrl_
              << " -> " << signalFilePath_ << std::endl;
    std::thread mqttThread([this]() { this->runMqttLoop(); });

    while (running_ && !stopRequested_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (mqttThread.joinable()) {
        mqttThread.join();
    }

    std::cout << "[SignalReceiver] Stopped." << std::endl;
}

void SignalReceiver::stop() {
    running_ = false;
    stopRequested_.store(true);
}

void SignalReceiver::runMqttLoop() {
    const std::string brokerUri = envOr("SDV_MQTT_BROKER_URI", "tcp://127.0.0.1:1883");
    const std::string vehicleId = envOr("SDV_VEHICLE_ID", "001");
    const std::string clientId = envOr("SDV_MQTT_CLIENT_ID", "sdv-signal-receiver-" + vehicleId);
    const std::string topic = "/sdv/vehicle/" + vehicleId + "/control/#";
    const std::string telemetryTopic = "/sdv/vehicle/" + vehicleId + "/telemetry";
    const int qos = 1;

    spdlog::info("[SignalReceiver] MQTT start: brokerUri={} clientId={} topic={} telemetryTopic={}",
                 brokerUri,
                 clientId,
                 topic,
                 telemetryTopic);

    mqtt::async_client client(brokerUri, clientId);
    mqtt::connect_options opts;
    opts.set_clean_start(false);
    opts.set_automatic_reconnect(std::chrono::seconds(1), std::chrono::seconds(10));

    try {
        client.start_consuming();
        client.connect(opts)->wait();
        client.subscribe(topic, qos)->wait();
        spdlog::info("[SignalReceiver] MQTT connected + subscribed");
    } catch (const std::exception& e) {
        spdlog::error("[SignalReceiver] MQTT connect/subscribe failed: {}", e.what());
        return;
    }

    while (running_ && !stopRequested_.load()) {
        auto msg = client.try_consume_message_for(std::chrono::milliseconds(200));
        if (!msg) continue;

        const std::string payload = msg->to_string();
        try {
            auto j = nlohmann::json::parse(payload);
            const std::string cmd = j.value("command", "");
            if (cmd != "KILL_SWITCH") {
                spdlog::warn("[SignalReceiver] ignore command='{}' payload={}", cmd, payload);
                continue;
            }
            const auto headerObj = j.contains("header") ? j["header"] : nlohmann::json::object();
            const std::string requestId = headerObj.value("msg_id", "");
            const auto payloadObj = j.contains("payload") ? j["payload"] : nlohmann::json::object();
            const std::string feature = payloadObj.value("feature_name", "");
            if (feature.empty()) {
                spdlog::warn("[SignalReceiver] missing payload.feature_name: payload={}", payload);
                continue;
            }

            SignalData data;
            data.type = "KILL";
            data.target = feature;
            data.payload = "";
            data.timestamp = std::chrono::system_clock::now();

            spdlog::warn("[SignalReceiver] rx KILL_SWITCH -> write inbound: {}", data.serialize());
            writeToFile(data);

            // Telemetry ACK for the control-tower (Spring) to complete the REST request.
            // Note: the actual stop is performed by FeatureEvaluator -> Feature process.
            // This ACK indicates "accepted/queued" semantics for demo.
            nlohmann::json t{
                    {"vehicle_id", vehicleId},
                    {"request_id", requestId},
                    {"command", cmd},
                    {"target_feature", feature},
                    {"version", payloadObj.value("version", "")},
                    {"status", "KILL_ACK"},
                    {"detail", "Feature stop requested"},
                    {"ts_ms", static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count())}
            };
            try {
                client.publish(telemetryTopic, t.dump(), qos, false);
                spdlog::info("[SignalReceiver] telemetry tx: topic={} requestId={} status=KILL_ACK feature={}",
                             telemetryTopic,
                             requestId,
                             feature);
            } catch (const std::exception& e) {
                spdlog::warn("[SignalReceiver] telemetry publish failed: {}", e.what());
            }
        } catch (const std::exception& e) {
            spdlog::warn("[SignalReceiver] MQTT payload parse failed: err={} payload={}", e.what(), payload);
        }
    }

    try {
        client.unsubscribe(topic)->wait();
        client.disconnect()->wait();
        client.stop_consuming();
    } catch (...) {
    }
}

}  // namespace sdv::platform::centercommunication

static std::atomic<bool> shutdownRequested(false);

void signalReceiverHandler(int) {
    shutdownRequested = true;
}

int main() {
    signal(SIGTERM, signalReceiverHandler);
    signal(SIGINT, signalReceiverHandler);

    sdv::platform::centercommunication::SignalReceiver receiver;

    std::thread receiverThread([&receiver]() {
        receiver.start();
    });

    while (!shutdownRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    receiver.stop();
    receiverThread.join();

    return 0;
}

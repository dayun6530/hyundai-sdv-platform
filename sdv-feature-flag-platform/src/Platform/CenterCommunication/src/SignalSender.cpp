#include "CenterCommunication/SignalSender.h"

#include "SdvSpacedConsole.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <cstdlib>
#include <string>
#include <fstream>

#include <mqtt/async_client.h>

#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/Event.h"
#include "FeatureCommunication/EventBus/EventId.h"

namespace sdv::platform::centercommunication {

SignalSender::SignalSender(const std::string& centerUrl,
                           const std::string& signalFilePath)
    : Signal(centerUrl, signalFilePath) {}

void SignalSender::start() {
    running_ = true;
    SDV_SPC_COUT << "[SignalSender] Started (EventBus->MQTT)." << std::endl;

    while (running_) {
        processOutboundSignals();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    SDV_SPC_COUT << "[SignalSender] Stopped." << std::endl;
}

void SignalSender::stop() {
    running_ = false;
}

void SignalSender::processOutboundSignals() {
    // Publishing is handled in main() to keep a single long-lived MQTT + EventBus client.
}

void SignalSender::sendToCenter(const SignalData& data) {
    SDV_SPC_COUT << "[SignalSender] Sending to center: " << data.serialize() << std::endl;
    httpPost("/api/signals", data.serialize());
}

}  // namespace sdv::platform::centercommunication

static std::atomic<bool> shutdownRequested(false);

void signalSenderHandler(int) {
    shutdownRequested = true;
}

namespace {

static std::string getenvOr(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    if (v == nullptr || *v == '\0') return fallback;
    return std::string(v);
}

static void appendLogLine(const std::string& fileName, const std::string& line) {
    const std::string dir = getenvOr("SDV_LOG_DIR", "logs");
    std::ofstream out(dir + "/" + fileName, std::ios::app);
    if (!out.is_open()) return;
    out << line << std::endl;
}

static const char* eventName(sdv::platform::communications::eventbus::EventId id) {
    using sdv::platform::communications::eventbus::EventId;
    switch (id) {
    case EventId::VehicleLocationUpdate: return "VehicleLocationUpdate";
    case EventId::RoadInfoUpdate: return "RoadInfoUpdate";
    case EventId::NavigationGuidanceUpdate: return "NavigationGuidanceUpdate";
    case EventId::AdasDecisionUpdate: return "AdasDecisionUpdate";
    case EventId::EnergyStateUpdate: return "EnergyStateUpdate";
    case EventId::FeatureFlagOn: return "FeatureFlagOn";
    case EventId::FeatureFlagOff: return "FeatureFlagOff";
    case EventId::KillSwitch: return "KillSwitch";
    case EventId::KillSwitchAck: return "KillSwitchAck";
    case EventId::ShadowMirrorData: return "ShadowMirrorData";
    case EventId::ShadowModeOn: return "ShadowModeOn";
    case EventId::ShadowModeOff: return "ShadowModeOff";
    case EventId::AbVariantAssign: return "AbVariantAssign";
    case EventId::ShadowPromotionRequest: return "ShadowPromotionRequest";
    case EventId::QuitTheSystem: return "QuitTheSystem";
    default: return "Unknown";
    }
}

static std::string toJson(const sdv::platform::communications::eventbus::Event& e) {
    const auto id = e.getEventId();
    const std::string& msg = e.getMessage();
    // Minimal JSON escaping for quotes/backslashes/newlines.
    std::string escaped;
    escaped.reserve(msg.size() + 8);
    for (char c : msg) {
        switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += c; break;
        }
    }

    return std::string("{\"event_id\":") + std::to_string(static_cast<int32_t>(id)) +
           ",\"event_name\":\"" + eventName(id) +
           "\",\"message\":\"" + escaped + "\"}";
}

static std::string killAckFeatureFromMessage(const std::string& msg) {
    const std::size_t bar = msg.find('|');
    if (bar == std::string::npos) return msg;
    return msg.substr(0, bar);
}

}  // namespace

#ifndef SDV_CENTERCOMM_NO_MAIN
int main(int argc, char** argv) {
    signal(SIGTERM, signalSenderHandler);
    signal(SIGINT, signalSenderHandler);

    // LifeCycleMgr passes:
    // - argv[1]: connect-friendly EventBus address for clients (e.g. 127.0.0.1:50051)
    // - argv[last]: car_<n> instance id (when running car_1..car_10)
    const std::string eventBusAddress =
        (argc > 1 && argv[1] && *argv[1] != '\0') ? std::string(argv[1]) : std::string("127.0.0.1:50051");
    std::string carId = "car_1";
    if (argc > 1 && argv[argc - 1]) {
        const std::string last = std::string(argv[argc - 1]);
        if (last.rfind("car_", 0) == 0) carId = last;
    }
    const std::string brokerUri = getenvOr("SDV_MQTT_BROKER_URI", "tcp://127.0.0.1:1884");
    const std::string publishTopic = getenvOr(
        "SDV_MQTT_SIGNAL_TX_TOPIC",
        "/sdv/vehicle/" + carId + "/signals/outbound");

    sdv::platform::communications::eventbus::EventBusProxy proxy(eventBusAddress);
    const int64_t componentId = proxy.registerComponent();
    if (componentId < 0) {
        SDV_SPC_CERR << "[SignalSender] EventBus registerComponent failed" << std::endl;
        return 2;
    }

    const std::string clientId = "sdv-" + carId + "-signal-tx";
    mqtt::async_client client(brokerUri, clientId);
    mqtt::connect_options conn;
    conn.set_clean_session(true);
    conn.set_keep_alive_interval(30);

    try {
        client.connect(conn)->wait();
        SDV_SPC_COUT << "[SignalSender] Connected. broker=" << brokerUri
                  << " topic=" << publishTopic
                  << " eventBus=" << eventBusAddress << std::endl;
    } catch (const std::exception& e) {
        SDV_SPC_CERR << "[SignalSender] MQTT connect failed: " << e.what() << std::endl;
        proxy.unRegister(componentId);
        return 3;
    }

    while (!shutdownRequested) {
        auto queue = proxy.getEventQueue(componentId);
        while (queue.getSize() > 0) {
            const auto e = queue.getEvent();
            const std::string payload = toJson(e);
            bool sent = false;
            for (int attempt = 0; attempt < 2 && !sent; ++attempt) {
                try {
                    if (!client.is_connected()) {
                        client.connect(conn)->wait();
                    }
                    client.publish(publishTopic, payload, 1, false)->wait();
                    sent = true;
                    appendLogLine("SignalSender.log", std::string("TX ") + payload);
                    if (e.getEventId() == sdv::platform::communications::eventbus::EventId::KillSwitchAck) {
                        const std::string feat = killAckFeatureFromMessage(e.getMessage());
                        SDV_SPC_COUT << "[SignalSender] Sent Message:KILL_ACK:" << feat << std::endl;
                    }
                } catch (const std::exception& ex) {
                    if (attempt == 1) {
                        SDV_SPC_CERR << "[SignalSender] publish failed: " << ex.what() << std::endl;
                        appendLogLine("SignalSender.log", std::string("TX_FAIL ") + ex.what());
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    try {
        client.disconnect()->wait();
    } catch (...) {
    }
    proxy.unRegister(componentId);

    return 0;
}
#endif  // SDV_CENTERCOMM_NO_MAIN

#include "CenterCommunication/MqttVehicleAgent.h"

#include <csignal>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

using sdv::platform::centercommunication::MqttVehicleAgent;
using sdv::platform::centercommunication::IFeatureController;

#include "FeatureCommunication/EventBus/Event.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

namespace sdv::platform::centercommunication {
namespace {

class EventBusFeatureController final : public IFeatureController {
public:
    explicit EventBusFeatureController(std::string eventBusAddress)
        : eventBusAddress_(std::move(eventBusAddress))
        , proxy_(eventBusAddress_) {}

    void stopFeature(const std::string& logicalName) override {
        using sdv::platform::communications::eventbus::Event;
        using sdv::platform::communications::eventbus::EventId;

        if (logicalName.empty()) {
            throw std::invalid_argument("logicalName must not be empty");
        }

        const bool ok = proxy_.sendEvent(Event(EventId::KillSwitch, logicalName));
        if (!ok) {
            throw std::runtime_error("EventBus sendEvent(KillSwitch) failed");
        }
        spdlog::warn("[MqttVehicleAgentHost] forwarded KillSwitch to EventBus: {}", logicalName);
    }

    void deployFeatureBinary(const std::string& logicalName,
                             const std::string& version,
                             const std::string& payloadUrl) override {
        // Keep existing deploy behavior in MqttVehicleAgent (simulation/stub) for now.
        (void)logicalName;
        (void)version;
        (void)payloadUrl;
        spdlog::info("[MqttVehicleAgentHost] deployFeatureBinary ignored (handled elsewhere)");
    }

private:
    std::string eventBusAddress_;
    sdv::platform::communications::eventbus::EventBusProxy proxy_;
};

static std::string resolveCarIdFromArgv(int argc, char** argv) {
    if (argc <= 1) return "car_1";
    // LifeCycleMgr passes instance id as the last argv when available.
    const std::string last = argv[argc - 1] ? std::string(argv[argc - 1]) : "";
    if (last.rfind("car_", 0) == 0) {
        return last;
    }
    return "car_1";
}

static std::string resolveEventBusAddressFromArgv(int argc, char** argv) {
    // LifeCycleMgr passes connect-friendly address as argv[1] for most processes.
    if (argc > 1 && argv[1] && *argv[1] != '\0') {
        return std::string(argv[1]);
    }
    return "127.0.0.1:50051";
}

static std::string resolveBrokerUri() {
    if (const char* env = std::getenv("SDV_MQTT_BROKER_URI"); env != nullptr && *env != '\0') {
        return std::string(env);
    }
    // Match kill-control-service default (1884) and local ws broker config.
    return "tcp://127.0.0.1:1884";
}

}  // namespace
}  // namespace sdv::platform::centercommunication

namespace {
std::atomic<bool> gStop{false};

void onSignal(int) {
    gStop.store(true);
}
}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    auto& agent = MqttVehicleAgent::instance();
    MqttVehicleAgent::Config cfg;
    const std::string carId = sdv::platform::centercommunication::resolveCarIdFromArgv(argc, argv);
    const std::string eventBusAddr = sdv::platform::centercommunication::resolveEventBusAddressFromArgv(argc, argv);

    cfg.brokerUri = sdv::platform::centercommunication::resolveBrokerUri();
    cfg.clientId = "sdv-" + carId + "-agent";
    cfg.subscribeTopic = "/sdv/vehicle/" + carId + "/control/#";
    cfg.publishTopic = "/sdv/vehicle/" + carId + "/telemetry";
    cfg.qos = 1;
    agent.configure(cfg);

    agent.setFeatureController(
        std::make_shared<sdv::platform::centercommunication::EventBusFeatureController>(eventBusAddr)
    );

    spdlog::info("[MqttVehicleAgentHost] starting agent");
    agent.start();

    while (!gStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    spdlog::info("[MqttVehicleAgentHost] stopping agent");
    agent.stop();
    return 0;
}


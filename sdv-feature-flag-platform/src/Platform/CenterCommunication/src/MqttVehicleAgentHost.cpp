#include "CenterCommunication/MqttVehicleAgent.h"

#include <csignal>
#include <chrono>
#include <cstdlib>
#include <thread>

#include <spdlog/spdlog.h>

using sdv::platform::centercommunication::MqttVehicleAgent;

namespace {
std::atomic<bool> gStop{false};

void onSignal(int) {
    gStop.store(true);
}
}  // namespace

int main() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    auto& agent = MqttVehicleAgent::instance();
    MqttVehicleAgent::Config cfg;
    if (const char* env = std::getenv("SDV_MQTT_BROKER_URI"); env != nullptr && *env != '\0') {
        cfg.brokerUri = env;
    } else {
        cfg.brokerUri = "tcp://127.0.0.1:1883";
    }
    cfg.clientId = "sdv-vehicle-001-agent";
    cfg.subscribeTopic = "/sdv/vehicle/001/control/#";
    cfg.publishTopic = "/sdv/vehicle/001/telemetry";
    cfg.qos = 1;
    agent.configure(cfg);

    spdlog::info("[MqttVehicleAgentHost] starting agent");
    agent.start();

    while (!gStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    spdlog::info("[MqttVehicleAgentHost] stopping agent");
    agent.stop();
    return 0;
}


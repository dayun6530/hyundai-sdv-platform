#include "LifeCycleManager/FeatureEvaluator.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <signal.h>

namespace sdv::platform::lifecyclemgr {

FeatureEvaluator::FeatureEvaluator(EventBusInterface& eventBus,
                                   const std::string& inboundPath,
                                   const std::string& outboundPath)
    : Feature("FeatureEvaluator", eventBus)
    , inboundPath_(inboundPath)
    , outboundPath_(outboundPath) {}

void FeatureEvaluator::execute() {
    log(LogLevel::info, "started. Inbound: " + inboundPath_ +
        " Outbound: " + outboundPath_);

    while (!isStopRequested()) {
        processSignals();
        pollEvents();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void FeatureEvaluator::processSignals() {
    std::ifstream input(inboundPath_);
    if (!input.is_open()) return;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    input.close();

    if (lines.empty()) return;

    std::ofstream clear(inboundPath_, std::ios::trunc);
    clear.close();

    for (const auto& cmd : lines) {
        auto firstColon = cmd.find(':');
        if (firstColon == std::string::npos) {
            log(LogLevel::warn, "invalid signal format: " + cmd);
            continue;
        }

        std::string action = cmd.substr(0, firstColon);
        std::string rest = cmd.substr(firstColon + 1);

        auto secondColon = rest.find(':');
        std::string featureName = (secondColon != std::string::npos)
            ? rest.substr(0, secondColon) : rest;

        if (action == "FLAG_ON") {
            sendFlagOn(featureName);
        } else if (action == "FLAG_OFF") {
            sendFlagOff(featureName);
        } else if (action == "KILL") {
            sendKillSwitch(featureName);
        } else {
            log(LogLevel::warn, "unknown action: " + action);
        }
    }
}

bool FeatureEvaluator::sendFlagOn(const std::string& featureName) {
    log(LogLevel::info, "FLAG_ON -> " + featureName);
    return eventBus_.sendEvent(Event(EventId::FeatureFlagOn, featureName));
}

bool FeatureEvaluator::sendFlagOff(const std::string& featureName) {
    log(LogLevel::info, "FLAG_OFF -> " + featureName);
    return eventBus_.sendEvent(Event(EventId::FeatureFlagOff, featureName));
}

bool FeatureEvaluator::sendKillSwitch(const std::string& featureName) {
    log(LogLevel::info, "KILL -> " + featureName);
    sendWarning(featureName, "KillSwitch activated");
    return eventBus_.sendEvent(Event(EventId::KillSwitch, featureName));
}

void FeatureEvaluator::sendWarning(const std::string& target,
                                   const std::string& message) {
    std::ofstream out(outboundPath_, std::ios::app);
    if (out.is_open()) {
        out << "WARNING:" << target << ":" << message << std::endl;
        log(LogLevel::info, "warning -> SignalSender: " + target + " " + message);
    }
}

}  // namespace sdv::platform::lifecyclemgr

int main(int argc, char* argv[]) {
    std::string address = "localhost:50051";
    if (argc > 1) address = argv[1];

    signal(SIGTERM, sdv::platform::lifecyclemgr::Feature::signalHandler);
    signal(SIGINT, sdv::platform::lifecyclemgr::Feature::signalHandler);

    sdv::platform::communications::eventbus::EventBusProxy proxy(address);
    sdv::platform::lifecyclemgr::FeatureEvaluator evaluator(proxy);
    return evaluator.run();
}

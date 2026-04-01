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

    // Give other features time to register to EventBus before emitting control events.
    // EventBus does not guarantee delivery to components that register later.
    std::this_thread::sleep_for(std::chrono::seconds(3));

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
        std::string extra = (secondColon != std::string::npos)
            ? rest.substr(secondColon + 1) : "";

        if (action == "FLAG_ON") {
            sendFlagOn(featureName);
        } else if (action == "FLAG_OFF") {
            sendFlagOff(featureName);
        } else if (action == "KILL") {
            sendKillSwitch(featureName);
<<<<<<< Updated upstream
        } else if (action == "SHADOW_ON") {
            sendShadowOn(featureName);
        } else if (action == "SHADOW_OFF") {
            sendShadowOff(featureName);
=======
        } else if (action == "AB_SET") {
            if (extra.size() != 1 || (extra[0] != 'A' && extra[0] != 'B')) {
                log(LogLevel::warn, "invalid AB_SET format (expected AB_SET:FeatureName:A|B): " + cmd);
                continue;
            }
            sendAbSet(featureName, extra[0]);
>>>>>>> Stashed changes
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

<<<<<<< Updated upstream
bool FeatureEvaluator::sendShadowOn(const std::string& primaryName) {
    log(LogLevel::info, "SHADOW_ON -> " + primaryName);
    return eventBus_.sendEvent(Event(EventId::ShadowModeOn, primaryName));
}

bool FeatureEvaluator::sendShadowOff(const std::string& primaryName) {
    log(LogLevel::info, "SHADOW_OFF -> " + primaryName);
    return eventBus_.sendEvent(Event(EventId::ShadowModeOff, primaryName));
=======
bool FeatureEvaluator::sendAbSet(const std::string& featureName, char variant) {
    const std::string msg = featureName + "|" + std::string(1, variant);
    log(LogLevel::info, std::string("AB_SET -> ") + msg);
    return eventBus_.sendEvent(Event(EventId::AbVariantAssign, msg));
>>>>>>> Stashed changes
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

#include "LifeCycleManager/FeatureEvaluator.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <algorithm>
#include <cctype>

namespace sdv::platform::lifecyclemgr {

namespace {

void trimInPlace(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || std::isspace(static_cast<unsigned char>(s.back())))) {
        s.pop_back();
    }
    auto first = std::find_if_not(s.begin(), s.end(), [](unsigned char c) {
        return std::isspace(c);
    });
    s.erase(s.begin(), first);
}

}  // namespace

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
        trimInPlace(line);
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
        trimInPlace(extra);
        trimInPlace(action);
        trimInPlace(featureName);

        if (action == "FLAG_ON") {
            sendFlagOn(featureName);
        } else if (action == "FLAG_OFF") {
            sendFlagOff(featureName);
        } else if (action == "KILL") {
            sendKillSwitch(featureName);
        } else if (
            // Demo-friendly canonical situation types
            action == "DMS_SLEEP" ||
            action == "ADAS_FATAL" ||
            action == "SENSOR_FATAL" ||
            action == "MANUAL_KILL" ||
            // Backward-compatible aliases
            action == "DRIVER_SLEEP" ||
            action == "DMS_DRIVER_SLEEP" ||
            action == "DRIVER_DROWSY") {
            // External situation signals (from SignalReceiver) mapped to KillSwitch.
            log(LogLevel::warn, action + " -> KILL " + featureName);
            sendKillSwitch(featureName);
        } else if (action == "SHADOW_ON") {
            sendShadowOn(featureName);
        } else if (action == "SHADOW_OFF") {
            sendShadowOff(featureName);
        } else if (action == "AB_SET") {
            if (extra.size() != 1 || (extra[0] != 'A' && extra[0] != 'B')) {
                log(LogLevel::warn,
                    "invalid AB_SET format (expected AB_SET:FeatureName:A or AB_SET:FeatureName:B): " + cmd);
                continue;
            }
            sendAbSet(featureName, extra[0]);
        } else {
            log(LogLevel::warn, "unknown action: " + action);
        }
    }
}

bool FeatureEvaluator::sendFlagOn(const std::string& featureName) {
    log(LogLevel::info, "FLAG_ON -> " + featureName);
    const bool ok = eventBus_.sendEvent(Event(EventId::FeatureFlagOn, featureName));
    if (!ok) {
        log(LogLevel::warn, "FLAG_ON EventBus sendEvent failed for " + featureName);
    }
    return ok;
}

bool FeatureEvaluator::sendFlagOff(const std::string& featureName) {
    log(LogLevel::info, "FLAG_OFF -> " + featureName);
    const bool ok = eventBus_.sendEvent(Event(EventId::FeatureFlagOff, featureName));
    if (!ok) {
        log(LogLevel::warn, "FLAG_OFF EventBus sendEvent failed for " + featureName);
    }
    return ok;
}

bool FeatureEvaluator::sendKillSwitch(const std::string& featureName) {
    log(LogLevel::info, "KILL -> " + featureName);
    sendWarning(featureName, "KillSwitch activated");
    const bool ok = eventBus_.sendEvent(Event(EventId::KillSwitch, featureName));
    if (!ok) {
        log(LogLevel::warn, "KILL EventBus sendEvent failed for " + featureName);
    }
    return ok;
}

bool FeatureEvaluator::sendShadowOn(const std::string& primaryName) {
    log(LogLevel::info, "SHADOW_ON -> " + primaryName);
    const bool ok = eventBus_.sendEvent(Event(EventId::ShadowModeOn, primaryName));
    if (!ok) {
        log(LogLevel::warn, "SHADOW_ON EventBus sendEvent failed for " + primaryName);
    }
    return ok;
}

bool FeatureEvaluator::sendShadowOff(const std::string& primaryName) {
    log(LogLevel::info, "SHADOW_OFF -> " + primaryName);
    const bool ok = eventBus_.sendEvent(Event(EventId::ShadowModeOff, primaryName));
    if (!ok) {
        log(LogLevel::warn, "SHADOW_OFF EventBus sendEvent failed for " + primaryName);
    }
    return ok;
}

bool FeatureEvaluator::sendAbSet(const std::string& featureName, char variant) {
    const std::string msg = featureName + "|" + std::string(1, variant);
    log(LogLevel::info, std::string("AB_SET -> ") + msg);
    const bool ok = eventBus_.sendEvent(Event(EventId::AbVariantAssign, msg));
    if (!ok) {
        log(LogLevel::warn, "AB_SET EventBus sendEvent failed for " + msg);
    }
    return ok;
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
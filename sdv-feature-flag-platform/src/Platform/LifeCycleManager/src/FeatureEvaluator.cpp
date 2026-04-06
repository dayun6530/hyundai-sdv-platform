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

bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    auto h = haystack.size();
    auto n = needle.size();
    if (n == 0 || h < n) return false;
    for (std::size_t i = 0; i <= h - n; ++i) {
        bool match = true;
        for (std::size_t j = 0; j < n; ++j) {
            if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool isPlatformKillSwitchTriggerText(const std::string& text) {
    return containsIgnoreCase(text, "Kill switch activated");
}

}  // namespace

FeatureEvaluator::FeatureEvaluator(EventBusInterface& eventBus,
                                   const std::string& inboundPath,
                                   const std::string& outboundPath)
    : Feature("FeatureEvaluator", eventBus)
    , inboundPath_(inboundPath)
    , outboundPath_(outboundPath) {}

void FeatureEvaluator::broadcastKillSwitchToAllTargetFeatures() {
    static constexpr const char* kTargets[] = {
        "NavigationFeature",
        "NavigationFeatureShadow",
        "AdasFeature",
        "AdasFeatureShadow",
        "EvEnergyFeature",
        "EvEnergyFeatureShadow",
        "WatchDog",
    };

    log(LogLevel::warn, "모든 Feature로 Kill Switch 이벤트 발송 완료");

    for (const char* target : kTargets) {
        sendKillSwitch(target);
    }
}

void FeatureEvaluator::execute() {
    log(LogLevel::info, "started. Inbound: " + inboundPath_ +
        " Outbound: " + outboundPath_);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    while (!isStopRequested()) {
        processSignals();

        auto appEvents = pollEvents();
        for (const auto& event : appEvents) {
            if (isPlatformKillSwitchTriggerText(event.getMessage())) {
                broadcastKillSwitchToAllTargetFeatures();
                break;
            }
        }

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

        if (isPlatformKillSwitchTriggerText(cmd)) {
            broadcastKillSwitchToAllTargetFeatures();
            continue;
        }

        auto firstColon = cmd.find(':');
        if (firstColon == std::string::npos) {
            log(LogLevel::warn, "invalid signal format: " + cmd);
            continue;
        }

        std::string action = cmd.substr(0, firstColon);
        std::string rest = cmd.substr(firstColon + 1);

        auto secondColon = rest.find(':');

        std::string featureName =
            (secondColon != std::string::npos) ? rest.substr(0, secondColon) : rest;

        std::string extra =
            (secondColon != std::string::npos) ? rest.substr(secondColon + 1) : "";

        trimInPlace(action);
        trimInPlace(featureName);
        trimInPlace(extra);

        // -------- FLAG --------
        if (action == "FLAG_ON") {
            if (extra.size() == 1 && (extra[0] == 'A' || extra[0] == 'B')) {
                sendFlagOnVariant(featureName, extra[0]);
            } else {
                sendFlagOn(featureName);
            }
        }
        else if (action == "FLAG_OFF") {
            if (extra.size() == 1 && (extra[0] == 'A' || extra[0] == 'B')) {
                sendFlagOffVariant(featureName, extra[0]);
            } else {
                sendFlagOff(featureName);
            }
        }

        // -------- KILL --------
        else if (action == "KILL") {
            sendKillSwitch(featureName);
        }

        // -------- 상황 기반 Kill --------
        else if (
            action == "DMS_SLEEP" ||
            action == "ADAS_FATAL" ||
            action == "SENSOR_FATAL" ||
            action == "MANUAL_KILL" ||
            action == "DRIVER_SLEEP" ||
            action == "DMS_DRIVER_SLEEP" ||
            action == "DRIVER_DROWSY") {

            log(LogLevel::warn, action + " -> KILL " + featureName);
            sendKillSwitch(featureName);
        }

        // -------- Shadow --------
        else if (action == "SHADOW_ON") {
            sendShadowOn(featureName);
        }
        else if (action == "SHADOW_OFF") {
            sendShadowOff(featureName);
        }

        // -------- A/B --------
        else if (action == "AB_SET") {
            if (extra.size() != 1 || (extra[0] != 'A' && extra[0] != 'B')) {
                log(LogLevel::warn, "invalid AB_SET format: " + cmd);
                continue;
            }
            sendAbSet(featureName, extra[0]);
        }

        else {
            log(LogLevel::warn, "unknown action: " + action);
        }
    }
}

// ======================== EVENT SEND ========================

bool FeatureEvaluator::sendFlagOn(const std::string& featureName) {
    log(LogLevel::info, "FLAG_ON -> " + featureName);
    return eventBus_.sendEvent(Event(EventId::FeatureFlagOn, featureName));
}

bool FeatureEvaluator::sendFlagOnVariant(const std::string& featureName, char variant) {
    std::string msg = featureName + "|" + std::string(1, variant);
    log(LogLevel::info, "FLAG_ON -> " + msg);
    return eventBus_.sendEvent(Event(EventId::FeatureFlagOn, msg));
}

bool FeatureEvaluator::sendFlagOff(const std::string& featureName) {
    log(LogLevel::info, "FLAG_OFF -> " + featureName);
    return eventBus_.sendEvent(Event(EventId::FeatureFlagOff, featureName));
}

bool FeatureEvaluator::sendFlagOffVariant(const std::string& featureName, char variant) {
    std::string msg = featureName + "|" + std::string(1, variant);
    log(LogLevel::info, "FLAG_OFF -> " + msg);
    return eventBus_.sendEvent(Event(EventId::FeatureFlagOff, msg));
}

bool FeatureEvaluator::sendKillSwitch(const std::string& featureName) {
    log(LogLevel::info, "KILL -> " + featureName);
    sendWarning(featureName, "KillSwitch activated");
    return eventBus_.sendEvent(Event(EventId::KillSwitch, featureName));
}

bool FeatureEvaluator::sendShadowOn(const std::string& primaryName) {
    log(LogLevel::info, "SHADOW_ON -> " + primaryName);
    return eventBus_.sendEvent(Event(EventId::ShadowModeOn, primaryName));
}

bool FeatureEvaluator::sendShadowOff(const std::string& primaryName) {
    log(LogLevel::info, "SHADOW_OFF -> " + primaryName);
    return eventBus_.sendEvent(Event(EventId::ShadowModeOff, primaryName));
}

bool FeatureEvaluator::sendAbSet(const std::string& featureName, char variant) {
    std::string msg = featureName + "|" + std::string(1, variant);
    log(LogLevel::info, "AB_SET -> " + msg);
    return eventBus_.sendEvent(Event(EventId::AbVariantAssign, msg));
}

void FeatureEvaluator::sendWarning(const std::string& target,
                                   const std::string& message) {
    std::ofstream out(outboundPath_, std::ios::app);
    if (out.is_open()) {
        out << "WARNING:" << target << ":" << message << std::endl;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string address = "localhost:50051";
    if (argc > 1) address = argv[1];

    signal(SIGTERM, sdv::platform::lifecyclemgr::Feature::signalHandler);
    signal(SIGINT, sdv::platform::lifecyclemgr::Feature::signalHandler);

    sdv::platform::communications::eventbus::EventBusProxy proxy(address);
    sdv::platform::lifecyclemgr::FeatureEvaluator evaluator(proxy, "input.txt", "output.txt");
    return evaluator.run();
}
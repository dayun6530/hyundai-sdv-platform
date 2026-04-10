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
#include <cstdlib>

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

/// External / relayed text that should trigger a platform-wide Kill Switch broadcast.
bool isPlatformKillSwitchTriggerText(const std::string& text) {
    return containsIgnoreCase(text, "Kill switch activated");
}

/// Primary base name → paired Shadow logical name (legacy `Foo`+`Shadow`, car-line `Foo_A`+`Foo_A_Shadow`).
std::string shadowLogicalNameForPrimaryBase(const std::string& base) {
    if (base.size() >= 2) {
        const bool endsA = (base.compare(base.size() - 2, 2, "_A") == 0);
        const bool endsB = (base.compare(base.size() - 2, 2, "_B") == 0);
        if (endsA || endsB) {
            return base + "_Shadow";
        }
    }
    return base + "Shadow";
}

}  // namespace

FeatureEvaluator::FeatureEvaluator(EventBusInterface& eventBus,
                                   const std::string& inboundPath,
                                   const std::string& outboundPath)
    : Feature("FeatureEvaluator", eventBus)
    , inboundPath_(inboundPath)
    , outboundPath_(outboundPath) {
    auto resolveLogPathIfDefault = [](const std::string& p) -> std::string {
        static constexpr const char kLogsPrefix[] = "logs/";
        if (p.rfind(kLogsPrefix, 0) != 0) {
            return p;
        }
        const char* envLogDir = std::getenv("SDV_LOG_DIR");
        const std::string logDir = (envLogDir != nullptr && *envLogDir != '\0')
            ? std::string(envLogDir)
            : std::string("logs");
        const std::size_t slash = p.find_last_of("/\\");
        const std::string base = (slash == std::string::npos) ? p : p.substr(slash + 1);
        return logDir + "/" + base;
    };

    inboundPath_ = resolveLogPathIfDefault(inboundPath_);
    outboundPath_ = resolveLogPathIfDefault(outboundPath_);
}

namespace {

static bool containsCarLineSuffix(const std::string& name) {
    return (name.find("_A") != std::string::npos) || (name.find("_B") != std::string::npos);
}

static bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
        s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Convenience: when running a single-car platform (car_A or car_B),
// allow signals like "KILL:AdasFeature" and map them to AdasFeature_A / AdasFeature_B.
static std::string normalizeCarLineTarget(const std::string& raw, std::optional<char> variantOpt) {
    if (!variantOpt.has_value()) return raw;
    const char v = *variantOpt;
    if (v != 'A' && v != 'B') return raw;
    if (raw.empty() || containsCarLineSuffix(raw)) return raw;

    // Handle legacy shadow names "FooShadow" -> "Foo_A_Shadow"
    if (endsWith(raw, "Shadow")) {
        const std::string base = raw.substr(0, raw.size() - std::string("Shadow").size());
        if (base.empty()) return raw;
        return base + "_" + std::string(1, v) + "_Shadow";
    }

    // Plain feature base name "Foo" -> "Foo_A"
    return raw + "_" + std::string(1, v);
}

}  // namespace

void FeatureEvaluator::emitAutoShadowModeForAllPrimaries() {
    static constexpr const char* kShadowPrimaries[] = {
        "NavigationFeature",
        "AdasFeature",
        "EvEnergyFeature",
        "NavigationFeature_A",
        "AdasFeature_A",
        "EvEnergyFeature_A",
        "NavigationFeature_B",
        "AdasFeature_B",
        "EvEnergyFeature_B",
    };
    log(LogLevel::info,
        "자동 Shadow Mode — 모든 Primary에 ShadowModeOn 발송 (수동 SHADOW_ON 생략)");
    for (const char* primary : kShadowPrimaries) {
        sendShadowOn(primary);
    }
}

void FeatureEvaluator::broadcastKillSwitchToAllTargetFeatures() {
    static constexpr const char* kTargets[] = {
        "NavigationFeature",
        "NavigationFeatureShadow",
        "AdasFeature",
        "AdasFeatureShadow",
        "EvEnergyFeature",
        "EvEnergyFeatureShadow",
        "NavigationFeature_A",
        "NavigationFeature_A_Shadow",
        "AdasFeature_A",
        "AdasFeature_A_Shadow",
        "EvEnergyFeature_A",
        "EvEnergyFeature_A_Shadow",
        "NavigationFeature_B",
        "NavigationFeature_B_Shadow",
        "AdasFeature_B",
        "AdasFeature_B_Shadow",
        "EvEnergyFeature_B",
        "EvEnergyFeature_B_Shadow",
        "WatchDog",
    };
    // 요구사항: 이벤트 전송 직전에 해당 로그를 남김
    log(LogLevel::warn, "모든 Feature로 Kill Switch 이벤트 발송 완료");
    for (const char* target : kTargets) {
        sendKillSwitch(target);
    }
}

void FeatureEvaluator::execute() {
    log(LogLevel::info, "started. Inbound: " + inboundPath_ +
        " Outbound: " + outboundPath_);

    // Give other features time to register to EventBus before emitting control events.
    // EventBus does not guarantee delivery to components that register later.
    std::this_thread::sleep_for(std::chrono::seconds(3));

    emitAutoShadowModeForAllPrimaries();

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
        std::string featureName = (secondColon != std::string::npos)
            ? rest.substr(0, secondColon) : rest;
        std::string extra = (secondColon != std::string::npos)
            ? rest.substr(secondColon + 1) : "";
        trimInPlace(extra);
        trimInPlace(action);
        trimInPlace(featureName);

        // Single-car convenience: infer _A/_B suffix when omitted.
        featureName = normalizeCarLineTarget(featureName, getAbVariant());

        if (action == "FLAG_ON") {
            sendFlagOn(featureName);
        } else if (action == "FLAG_OFF") {
            sendFlagOff(featureName);
        } else if (action == "KILL") {
            static constexpr const char kKillAllSuffix[] = "_ALL";
            constexpr std::size_t kAllLen = sizeof(kKillAllSuffix) - 1;
            if (featureName.size() > kAllLen &&
                featureName.compare(featureName.size() - kAllLen, kAllLen, kKillAllSuffix) == 0) {
                const std::string base = featureName.substr(0, featureName.size() - kAllLen);
                const std::string shadowName = shadowLogicalNameForPrimaryBase(base);
                log(LogLevel::warn,
                    "KILL_ALL -> Shadow " + shadowName + " then Primary " + base);
                // Shadow first: each queue processes Shadow-target kill before Primary-target,
                // so paired Shadow exits cleanly; Primary ignores the Shadow kill line.
                sendKillSwitch(shadowName);
                sendKillSwitch(base);
            } else {
                sendKillSwitch(featureName);
            }
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
    sdv::platform::lifecyclemgr::Feature::applyLaunchAbVariantFromArgv(argc, argv, evaluator.getFlag());
    return evaluator.run();
}

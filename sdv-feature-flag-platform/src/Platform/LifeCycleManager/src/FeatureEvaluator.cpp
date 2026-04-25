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
#include <unordered_set>
#include <vector>

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

// Convenience: when running a single-car platform (car_<n>),
// allow signals like "KILL:AdasFeature" and map them to AdasFeature_A / AdasFeature_B.
static std::string normalizeCarLineTarget(const std::string& raw, std::optional<char> variantOpt) {
    if (!variantOpt.has_value()) return raw;
    const char v = *variantOpt;
    if (v != 'A' && v != 'B') return raw;
    if (raw.empty() || containsCarLineSuffix(raw)) return raw;

    // Car-line shadow: "Foo_Shadow" where Foo has no _A/_B -> "Foo_A_Shadow" / "Foo_B_Shadow".
    // Must run before the legacy "…Shadow" suffix rule, otherwise "AdasFeature_Shadow" is parsed
    // as legacy base "AdasFeature_" and becomes "AdasFeature__A_Shadow" (wrong).
    static constexpr const char kCarLineShadowSuffix[] = "_Shadow";
    if (endsWith(raw, kCarLineShadowSuffix)) {
        const std::string base =
            raw.substr(0, raw.size() - (sizeof(kCarLineShadowSuffix) - 1));
        if (base.empty() || containsCarLineSuffix(base)) {
            return raw;
        }
        return base + "_" + std::string(1, v) + kCarLineShadowSuffix;
    }

    // Handle legacy shadow names "FooShadow" -> "Foo_A_Shadow"
    if (endsWith(raw, "Shadow")) {
        const std::string base = raw.substr(0, raw.size() - std::string("Shadow").size());
        if (base.empty()) return raw;
        return base + "_" + std::string(1, v) + "_Shadow";
    }

    // Plain feature base name "Foo" -> "Foo_A"
    return raw + "_" + std::string(1, v);
}

/// Reads \c feature lines from the config path in \c SDV_LIFECYCLE_CONFIG (set by LifeCycleMgr).
static std::unordered_set<std::string> loadFeatureExecNamesFromLifecycleConfig() {
    std::unordered_set<std::string> names;
    const char* env = std::getenv("SDV_LIFECYCLE_CONFIG");
    if (env == nullptr || *env == '\0') {
        return names;
    }
    std::ifstream file(env);
    if (!file.is_open()) {
        return names;
    }
    std::string line;
    while (std::getline(file, line)) {
        trimInPlace(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream iss(line);
        std::string key;
        std::string value;
        if (!(iss >> key) || key[0] == '#') {
            continue;
        }
        if (!(iss >> value)) {
            continue;
        }
        if (key == "feature") {
            names.insert(std::move(value));
        }
    }
    return names;
}

/// Maps shorthand KILL targets (e.g. \c AdasFeature) to concrete exec names present in config.
/// Returns empty if \p raw is not a known shorthand or nothing from the shorthand matches config.
static std::vector<std::string> resolveCarLineKillTargetsUsingConfig(
    const std::string& raw,
    const std::unordered_set<std::string>& deployed) {
    std::vector<std::string> matches;
    static constexpr const char* kDomains[] = {
        "NavigationFeature",
        "AdasFeature",
        "EvEnergyFeature",
    };
    for (const char* d : kDomains) {
        const std::string dom(d);
        if (raw == dom) {
            const std::string a = dom + "_A";
            const std::string b = dom + "_B";
            if (deployed.count(a)) {
                matches.push_back(a);
            }
            if (deployed.count(b)) {
                matches.push_back(b);
            }
            return matches;
        }
        if (raw == dom + "_Shadow") {
            const std::string as = dom + "_A_Shadow";
            const std::string bs = dom + "_B_Shadow";
            if (deployed.count(as)) {
                matches.push_back(as);
            }
            if (deployed.count(bs)) {
                matches.push_back(bs);
            }
            const std::string leg = dom + "Shadow";
            if (deployed.count(leg)) {
                matches.push_back(leg);
            }
            return matches;
        }
        if (raw == dom + "Shadow") {
            const std::string as = dom + "_A_Shadow";
            const std::string bs = dom + "_B_Shadow";
            if (deployed.count(as)) {
                matches.push_back(as);
            }
            if (deployed.count(bs)) {
                matches.push_back(bs);
            }
            if (deployed.count(dom + "Shadow")) {
                matches.push_back(dom + "Shadow");
            }
            return matches;
        }
    }
    return {};
}

/// Fallback when \c SDV_LIFECYCLE_CONFIG is unset/unreadable or shorthand matched nothing in config:
/// KILL both A and B car-line names for the domain.
static std::vector<std::string> expandAmbiguousCarLineKillTargets(const std::string& raw) {
    static constexpr const char* kDomains[] = {
        "NavigationFeature",
        "AdasFeature",
        "EvEnergyFeature",
    };
    for (const char* d : kDomains) {
        const std::string dom(d);
        if (raw == dom) {
            return {dom + "_A", dom + "_B"};
        }
        if (raw == dom + "_Shadow") {
            return {dom + "_A_Shadow", dom + "_B_Shadow"};
        }
        if (raw == dom + "Shadow") {
            return {dom + "_A_Shadow", dom + "_B_Shadow"};
        }
    }
    return {};
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

        const std::string rawTarget = featureName;
        const std::string normalizedTarget =
            normalizeCarLineTarget(rawTarget, getAbVariant());

        if (action == "FLAG_ON") {
            sendFlagOn(normalizedTarget);
        } else if (action == "FLAG_OFF") {
            sendFlagOff(normalizedTarget);
        } else if (action == "KILL") {
            const std::unordered_set<std::string> deployed =
                loadFeatureExecNamesFromLifecycleConfig();
            std::vector<std::string> killTargets =
                resolveCarLineKillTargetsUsingConfig(rawTarget, deployed);
            if (killTargets.empty()) {
                killTargets = expandAmbiguousCarLineKillTargets(rawTarget);
            }
            if (killTargets.empty()) {
                killTargets.push_back(normalizedTarget);
            }
            static constexpr const char kKillAllSuffix[] = "_ALL";
            constexpr std::size_t kAllLen = sizeof(kKillAllSuffix) - 1;
            for (const std::string& killName : killTargets) {
                if (killName.size() > kAllLen &&
                    killName.compare(killName.size() - kAllLen, kAllLen, kKillAllSuffix) == 0) {
                    const std::string base = killName.substr(0, killName.size() - kAllLen);
                    const std::string shadowName = shadowLogicalNameForPrimaryBase(base);
                    log(LogLevel::warn,
                        "KILL_ALL -> Shadow " + shadowName + " then Primary " + base);
                    // Shadow first: each queue processes Shadow-target kill before Primary-target,
                    // so paired Shadow exits cleanly; Primary ignores the Shadow kill line.
                    sendKillSwitch(shadowName);
                    sendKillSwitch(base);
                } else {
                    sendKillSwitch(killName);
                }
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
            log(LogLevel::warn, action + " -> KILL " + normalizedTarget);
            sendKillSwitch(normalizedTarget);
        } else if (action == "SHADOW_ON") {
            sendShadowOn(normalizedTarget);
        } else if (action == "SHADOW_OFF") {
            sendShadowOff(normalizedTarget);
        } else if (action == "SHADOW_PROMOTE") {
            sendShadowPromotionRequest(normalizedTarget);
        } else if (action == "AB_SET") {
            if (extra.size() != 1 || (extra[0] != 'A' && extra[0] != 'B')) {
                log(LogLevel::warn,
                    "invalid AB_SET format (expected AB_SET:FeatureName:A or AB_SET:FeatureName:B): " + cmd);
                continue;
            }
            sendAbSet(normalizedTarget, extra[0]);
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

bool FeatureEvaluator::sendShadowPromotionRequest(const std::string& primaryLogicalName) {
    log(LogLevel::info, "SHADOW_PROMOTE -> " + primaryLogicalName +
        " (Primary stops; Shadow observer exits — relaunch via LifeCycleMgr)");
    sendWarning(primaryLogicalName, "ShadowPromotionRequest");
    const bool ok = eventBus_.sendEvent(
        Event(EventId::ShadowPromotionRequest, primaryLogicalName));
    if (!ok) {
        log(LogLevel::warn,
            "SHADOW_PROMOTE EventBus sendEvent failed for " + primaryLogicalName);
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

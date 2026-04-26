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

#ifdef SDV_WITH_UNLEASH
#include "unleash/Client/unleashClient.hpp"
#include "unleash/Configuration/clientConfig.hpp"
#include "unleash/Domain/context.hpp"
#endif

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

#ifdef SDV_WITH_UNLEASH
    const char* unleashUrlEnv = std::getenv("UNLEASH_URL");
    const char* unleashKeyEnv = std::getenv("UNLEASH_API_KEY");
    const char* unleashAppEnv = std::getenv("UNLEASH_APP_NAME");
    const std::string unleashUrl =
        (unleashUrlEnv && *unleashUrlEnv) ? std::string(unleashUrlEnv) : std::string();
    const std::string unleashKey =
        (unleashKeyEnv && *unleashKeyEnv) ? std::string(unleashKeyEnv) : std::string();
    const std::string unleashApp =
        (unleashAppEnv && *unleashAppEnv) ? std::string(unleashAppEnv) : std::string("sdv-platform");

    std::unique_ptr<unleash::UnleashClient> unleashClient;
    if (!unleashUrl.empty() && !unleashKey.empty()) {
        try {
            using namespace std::chrono;
            unleash::ClientConfig config(unleashUrl, unleashKey, unleashApp);
            config.setTimeOutQueryMS(milliseconds(3000))
                  .setRefreshInterval(seconds(3))
                  .setMetricsInterval(seconds(15))
                  .setMetricsIntervalInitial(seconds(5));
            unleash::Context ctx;
            unleashClient = std::make_unique<unleash::UnleashClient>(config, ctx);
            unleashClient->onError([this](const unleash::EventHandler::ClientError& e) {
                log(LogLevel::warn, std::string("Unleash error: ") + e.message + " | " + e.details);
            }).onReady([this]() {
                log(LogLevel::info, "Unleash client ready");
            }).onUpdate([this]() {
                log(LogLevel::info, "Unleash toggles updated");
            });
            unleashClient->start();
            log(LogLevel::info, "Unleash enabled: " + unleashUrl + " (app=" + unleashApp + ")");
        } catch (const std::exception& e) {
            log(LogLevel::warn, std::string("Unleash init failed: ") + e.what());
            unleashClient.reset();
        }
    } else {
        log(LogLevel::info, "Unleash disabled (set UNLEASH_URL and UNLEASH_API_KEY to enable)");
    }

    auto lastNav = std::optional<bool>();
    auto lastAdas = std::optional<bool>();
    auto lastEnergy = std::optional<bool>();
#endif

    while (!isStopRequested()) {
        processSignals();
        auto appEvents = pollEvents();
        for (const auto& event : appEvents) {
            if (isPlatformKillSwitchTriggerText(event.getMessage())) {
                broadcastKillSwitchToAllTargetFeatures();
                break;
            }
        }

#ifdef SDV_WITH_UNLEASH
        if (unleashClient) {
            // Map Unleash flags to SDV logical feature names (base names; SignalReceiver expands to A/B as needed).
            const bool nav = unleashClient->isEnabled("NavigationFeature");
            const bool adas = unleashClient->isEnabled("AdasFeature");
            const bool energy = unleashClient->isEnabled("EvEnergyFeature");

            auto syncOne = [this](const char* name, bool enabled, std::optional<bool>& last) {
                if (last.has_value() && *last == enabled) return;
                last = enabled;
                if (enabled) {
                    sendFlagOn(name);
                } else {
                    sendFlagOff(name);
                }
            };

            syncOne("NavigationFeature", nav, lastNav);
            syncOne("AdasFeature", adas, lastAdas);
            syncOne("EvEnergyFeature", energy, lastEnergy);
        }
#endif

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

#ifdef SDV_WITH_UNLEASH
    if (unleashClient) {
        try {
            unleashClient->stop();
        } catch (...) {
        }
    }
#endif
}

void FeatureEvaluator::processSignals() {
    // MQTT-only flow: disable file-based signal ingress.
    // All control events should arrive through EventBus (e.g., from MqttVehicleAgentHost).
    // Keeping the method avoids touching call sites and preserves internal loop cadence.
    return;
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
    // MQTT-only flow: avoid file-based warning fan-out. Keep logs for observability.
    log(LogLevel::info, "warning: " + target + " " + message);
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

#include "LifeCycleManager/Feature.h"
#include "LifeCycleManager/TelemetryRuntimeConfig.h"
#include "LifeCycleManager/FleetVersionResolve.h"

#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <algorithm>
#include <iomanip>
#include <chrono>
#include <unistd.h>
#include <sys/stat.h>
#include <thread>
#include <signal.h>
#include <charconv>
#include <system_error>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <ctime>

namespace sdv::platform::lifecyclemgr {

namespace {

bool matchesMainFlagMessage(const std::string& msg, const std::string& processName) {
    return msg == processName;
}

std::optional<std::string> extractSubFeatureKeyFromMessage(const std::string& msg,
    const std::string& processName) {
    const std::string prefix = processName + ".";
    if (msg.rfind(prefix, 0) == 0 && msg.size() > prefix.size()) {
        return msg.substr(prefix.size());
    }
    return std::nullopt;
}

/// Reads one string field inside `features.<FEATURE_KEY>` in `deploy/state/<car>.json` (best-effort).
static std::optional<std::string> readQuotedFieldInFeatureBlock(const std::string& json,
    const char* featureKey,
    const char* field) {
    const std::string marker = std::string("\"") + featureKey + "\"";
    const std::size_t start = json.find(marker);
    if (start == std::string::npos) {
        return std::nullopt;
    }
    std::size_t end = std::string::npos;
    if (std::strcmp(featureKey, "ADAS") == 0) {
        end = json.find("\"Navigation\"", start + 1);
    } else if (std::strcmp(featureKey, "Navigation") == 0) {
        end = json.find("\"Energy\"", start + 1);
    } else if (std::strcmp(featureKey, "Energy") == 0) {
        end = json.find("\"ota\"", start + 1);
    }
    if (end == std::string::npos) {
        end = json.size();
    }
    const std::string block = json.substr(start, end - start);
    const std::string key = std::string("\"") + field + "\"";
    const std::size_t k = block.find(key);
    if (k == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t colon = block.find(':', k);
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::size_t i = colon + 1;
    while (i < block.size() && (block[i] == ' ' || block[i] == '\t')) {
        ++i;
    }
    if (i >= block.size() || block[i] != '"') {
        return std::nullopt;
    }
    ++i;
    const std::size_t valStart = i;
    while (i < block.size() && block[i] != '"') {
        ++i;
    }
    if (i <= valStart) {
        return std::nullopt;
    }
    return block.substr(valStart, i - valStart);
}

/// Reads `domains.<domainKey>.<field>` in `deploy/vehicle_runtime/<car>.json` (best-effort).
static std::optional<std::string> readQuotedFieldInDomainBlock(const std::string& json,
    const char* domainKey,
    const char* field) {
    const std::string marker = std::string("\"") + domainKey + "\"";
    const std::size_t start = json.find(marker);
    if (start == std::string::npos) {
        return std::nullopt;
    }
    std::size_t end = std::string::npos;
    if (std::strcmp(domainKey, "adas") == 0) {
        end = json.find("\"navigation\"", start + 1);
    } else if (std::strcmp(domainKey, "navigation") == 0) {
        end = json.find("\"evenergy\"", start + 1);
    } else if (std::strcmp(domainKey, "evenergy") == 0) {
        end = std::min(json.size(), start + 8192);
    }
    if (end == std::string::npos) {
        end = json.size();
    }
    const std::string block = json.substr(start, end - start);
    const std::string key = std::string("\"") + field + "\"";
    const std::size_t k = block.find(key);
    if (k == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t colon = block.find(':', k);
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::size_t i = colon + 1;
    while (i < block.size() && (block[i] == ' ' || block[i] == '\t')) {
        ++i;
    }
    if (i >= block.size() || block[i] != '"') {
        return std::nullopt;
    }
    ++i;
    const std::size_t valStart = i;
    while (i < block.size() && block[i] != '"') {
        ++i;
    }
    if (i <= valStart) {
        return std::nullopt;
    }
    return block.substr(valStart, i - valStart);
}

static std::string vehicleRuntimeJsonPathForCar(const std::string& statePath, const std::string& car) {
    if (const char* p = std::getenv("SDV_VEHICLE_RUNTIME_JSON")) {
        if (p[0] != '\0') {
            return std::string(p);
        }
    }
    if (const char* deployRoot = std::getenv("SDV_PLATFORM_DEPLOY_ROOT")) {
        if (deployRoot[0] != '\0') {
            std::string root(deployRoot);
            while (!root.empty() && (root.back() == '/' || root.back() == '\\')) {
                root.pop_back();
            }
            return root + "/../../deploy/vehicle_runtime/" + car + ".json";
        }
    }
    const std::string needle = "/platform/deploy/state/";
    const auto pos = statePath.find(needle);
    if (pos != std::string::npos) {
        return statePath.substr(0, pos) + "/deploy/vehicle_runtime/" + car + ".json";
    }
    // Fallback: when running from `sdv-feature-flag-platform` with a relative state path,
    // the docker/OTA runtime JSON lives in the sibling repo `sdv-ota-manager`.
    if (statePath.rfind("deploy/state/", 0) == 0 || statePath == "deploy/state" ||
        statePath.find("/deploy/state/") != std::string::npos) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == nullptr) {
            return std::string("../sdv-ota-manager/deploy/vehicle_runtime/") + car + ".json";
        }
        const std::string rel = std::string("/sdv-ota-manager/deploy/vehicle_runtime/") + car + ".json";
        const std::string base(cwd);
        const std::string cands[] = {
            base + "/.." + rel,
            base + "/../.." + rel,
            base + "/../../.." + rel,
            base + "/../../../.." + rel,
        };
        for (const auto& p : cands) {
            if (access(p.c_str(), R_OK) == 0) {
                return p;
            }
        }
        return std::string("../sdv-ota-manager/deploy/vehicle_runtime/") + car + ".json";
    }
    return {};
}

static std::string normalizeVersionForDeployBanner(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    if (s.empty()) {
        return s;
    }
    if (s[0] == 'v' || s[0] == 'V') {
        if (s.size() >= 2 && std::isdigit(static_cast<unsigned char>(s[1]))) {
            return std::string("v") + s.substr(1);
        }
    }
    if (std::isdigit(static_cast<unsigned char>(s[0]))) {
        return std::string("v") + s;
    }
    return s;
}

static std::optional<std::string> readRuntimeVersionForStartedLine(const std::string& statePath,
    const std::string& car,
    const std::string& processName,
    bool shadowObserver) {
    struct Row {
        const char* processName;
        const char* runtimeDomainKey;
        const char* runtimeVersionFieldPrimary;
        const char* runtimeVersionFieldShadow;
    };
    static const Row kRows[] = {
        {"AdasFeature", "adas", "primary_version", "shadow_version"},
        {"AdasFeatureShadow", "adas", "primary_version", "shadow_version"},
        {"NavigationFeature", "navigation", "primary_version", "shadow_version"},
        {"NavigationFeatureShadow", "navigation", "primary_version", "shadow_version"},
        {"EvEnergyFeature", "evenergy", "primary_version", "shadow_version"},
        {"EvEnergyFeatureShadow", "evenergy", "primary_version", "shadow_version"},
    };
    const Row* row = nullptr;
    for (const auto& r : kRows) {
        if (processName == r.processName) {
            row = &r;
            break;
        }
    }
    if (row == nullptr) {
        return std::nullopt;
    }

    const char* field = shadowObserver ? row->runtimeVersionFieldShadow : row->runtimeVersionFieldPrimary;
    const std::string rtPath = vehicleRuntimeJsonPathForCar(statePath, car);
    if (rtPath.empty()) {
        return std::nullopt;
    }
    std::ifstream rt(rtPath);
    if (!rt) {
        return std::nullopt;
    }
    std::ostringstream rtbuf;
    rtbuf << rt.rdbuf();
    auto ver = readQuotedFieldInDomainBlock(rtbuf.str(), row->runtimeDomainKey, field);
    if (!ver || ver->empty()) {
        return std::nullopt;
    }
    // For started-line we want plain numeric tokens ("2.0"), not "v2.0".
    std::string v = *ver;
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) {
        if (v.size() >= 2 && std::isdigit(static_cast<unsigned char>(v[1]))) {
            v.erase(0, 1);
        }
    }
    if (v.empty()) {
        return std::nullopt;
    }
    return v;
}

}  // namespace

std::atomic<bool> Feature::stopRequested_(false);

namespace {

static std::string jsonEscapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

static std::string nowUtcIso8601Z() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return os.str();
}

static bool looksLikeJsonValue(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    if (i >= s.size()) return false;
    const char c = s[i];
    return (c == '{' || c == '[' || c == '"' || c == 't' || c == 'f' || c == 'n' ||
            (c >= '0' && c <= '9') || c == '-');
}

}  // namespace

std::string Feature::normalizeFileName(const std::string& name) {
    std::string result;
    for (char c : name) {
        if (c != ' ') {
            result += c;
        }
    }
    return result;
}

Feature::Feature(const std::string& name, EventBusInterface& eventBus)
    : eventBus_(eventBus)
    , name_(name)
    , primaryName_(name)
    , componentId_(-1)
    , flag_(name) {
    const char* envLogDir = std::getenv("SDV_LOG_DIR");
    const std::string logDir = (envLogDir != nullptr && *envLogDir != '\0')
        ? std::string(envLogDir)
        : std::string("logs");

    // Ensure nested log dir exists (logs/ then logs/car_<n>/).
    mkdir("logs", 0755);
    if (logDir != "logs") {
        mkdir(logDir.c_str(), 0755);
    }

    logFilePath_ = logDir + "/" + normalizeFileName(name) + ".log";
    bool appendAfterExisting = false;
    struct stat st {};
    if (stat(logFilePath_.c_str(), &st) == 0 && st.st_size > 0) {
        appendAfterExisting = true;
    }
    logFile_.open(logFilePath_, std::ios::out | std::ios::app);
    if (appendAfterExisting && logFile_.is_open()) {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        const std::tm* tm = std::localtime(&t);
        if (tm != nullptr) {
            logFile_ << "\n[" << std::put_time(tm, "%Y-%m-%d %H:%M:%S")
                     << "] [INFO ] [LOG] --- process restarted; prior lines above ---\n";
            logFile_.flush();
        }
    }
}

Feature::~Feature() {
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

void Feature::maybeLogFleetFeatureDeployBanner() {
    struct BannerRow {
        const char* processName;
        const char* jsonFeatureKey;
        const char* jsonField;
        const char* logPrefix;
        const char* runtimeDomainKey;
        const char* runtimeVersionField;
    };
    static const BannerRow kRows[] = {
        {"AdasFeature", "ADAS", "active_version", "AdasFeature_", "adas", "primary_version"},
        {"AdasFeatureShadow", "ADAS", "shadow_version", "AdasFeature_Shadow_", "adas", "shadow_version"},
        {"NavigationFeature", "Navigation", "active_version", "NavigationFeature_", "navigation", "primary_version"},
        {"NavigationFeatureShadow", "Navigation", "shadow_version", "NavigationFeature_Shadow_", "navigation",
            "shadow_version"},
        {"EvEnergyFeature", "Energy", "active_version", "EvEnergyFeature_", "evenergy", "primary_version"},
        {"EvEnergyFeatureShadow", "Energy", "shadow_version", "EvEnergyFeature_Shadow_", "evenergy", "shadow_version"},
    };
    const BannerRow* row = nullptr;
    for (const auto& r : kRows) {
        if (name_ == r.processName) {
            row = &r;
            break;
        }
    }
    if (row == nullptr) {
        return;
    }

    const char* car = std::getenv("SDV_CAR_INSTANCE");
    if (car == nullptr || car[0] == '\0') {
        return;
    }

    std::string statePath;
    if (const char* envRoot = std::getenv("SDV_PLATFORM_DEPLOY_ROOT")) {
        if (envRoot[0] != '\0') {
            statePath = std::string(envRoot) + "/state/" + car + ".json";
        }
    }
    if (statePath.empty()) {
        statePath = std::string("deploy/state/") + car + ".json";
    }
    std::ifstream in(statePath);
    if (!in) {
        return;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string fleetJson = buf.str();

    std::optional<std::string> ver;
    const std::string rtPath = vehicleRuntimeJsonPathForCar(statePath, car);
    if (!rtPath.empty()) {
        std::ifstream rt(rtPath);
        if (rt) {
            std::ostringstream rtbuf;
            rtbuf << rt.rdbuf();
            ver = readQuotedFieldInDomainBlock(rtbuf.str(), row->runtimeDomainKey, row->runtimeVersionField);
        }
    }
    if (!ver || ver->empty()) {
        ver = readQuotedFieldInFeatureBlock(fleetJson, row->jsonFeatureKey, row->jsonField);
    }
    if (!ver || ver->empty()) {
        return;
    }

    const std::string canon = normalizeVersionForDeployBanner(*ver);
    if (canon.empty()) {
        return;
    }

    const std::string msg = std::string(row->logPrefix) + canon + " is deployed!";
    log(LogLevel::info, msg);
    std::cerr << "[VERSION] " << msg << std::endl;
}

int Feature::run() {
    flag_.resetRunState();

    componentId_ = eventBus_.registerComponent();
    if (componentId_ <= 0) {
        log(LogLevel::error, "EventBus registration failed. Terminating.");
        return 1;
    }
    std::string distVer = distributionVersionLabelForLog();
    const char* car = std::getenv("SDV_CAR_INSTANCE");
    if (car != nullptr && car[0] != '\0') {
        carId_ = std::string(car);
        std::string configPath;
        if (const char* cfg = std::getenv("SDV_LIFECYCLE_CONFIG")) {
            if (cfg[0] != '\0') {
                configPath = cfg;
            }
        }
        const bool shadowProc = (name_.size() >= 6 && name_.compare(name_.size() - 6, 6, "Shadow") == 0);
        if (auto rt = readVehicleRuntimeDomainVersions(name_, carId_)) {
            const std::string rtLine =
                formatVehicleRuntimeVersionLogLine(*rt, carId_, name_, shadowProc);
            log(LogLevel::info, rtLine);
            std::cerr << rtLine << std::endl;
            appendRunSdvVersionLog(carId_, rtLine);
            const std::string& laneVer = shadowProc ? rt->shadow_version : rt->primary_version;
            if (!laneVer.empty()) {
                distVer = stripLeadingVForDisplay(laneVer);
            }
        }
        if (auto info = resolveFleetVersionForProcess(name_, carId_, configPath)) {
            if (distVer.empty() && info->version != "unknown") {
                distVer = stripLeadingVForDisplay(info->version);
            }
            const std::string verLine = formatVersionLogLine(*info, carId_);
            const std::string startedLine = formatFeatureStartedLogLine(*info, carId_);
            log(LogLevel::info, verLine);
            log(LogLevel::info, startedLine);
            std::cerr << verLine << std::endl;
            std::cerr << startedLine << std::endl;
            appendRunSdvVersionLog(carId_, verLine);
            appendRunSdvVersionLog(carId_, startedLine);
        }
    }
    effectiveVersionLabelForLog_ = distVer;
    const std::string verSuffix =
        distVer.empty() ? std::string() : (std::string(" — ") + name_ + " v" + distVer + " is running");

    if (flag_.isEnabled()) {
        log(LogLevel::info, "started (ID:" + std::to_string(componentId_) +
            ", PID:" + std::to_string(getpid()) +
            ", Flag:ON)" + verSuffix);
    } else {
        log(LogLevel::info, "registered (ID:" + std::to_string(componentId_) +
            ", PID:" + std::to_string(getpid()) +
            ") — FLAG_ON 대기 중" + verSuffix);
    }

    if (isShadowMode_) {
        log(LogLevel::info, std::string("Shadow observer — mirroring Primary: ") + primaryName_);
        runShadowMirrorObserveLoop();
    } else {
        execute();
    }

    eventBus_.unRegister(componentId_);
    log(LogLevel::info, "stopped");
    return 0;
}

bool Feature::sendTelemetry(const std::string& level,
                            const std::string& eventType,
                            const std::string& jsonPayload) {
    // Telemetry is allowed for both Primary and Shadow observer processes.
    // It is a passive data stream and must not control the vehicle.
    const std::string ts = nowUtcIso8601Z();
    const std::string carId = !carId_.empty()
        ? carId_
        : (std::getenv("SDV_CAR_INSTANCE") ? std::string(std::getenv("SDV_CAR_INSTANCE")) : std::string(""));

    std::ostringstream os;
    os << "{";
    os << "\"timestamp\":\"" << jsonEscapeString(ts) << "\",";
    os << "\"car_id\":\"" << jsonEscapeString(carId) << "\",";
    os << "\"feature\":\"" << jsonEscapeString(name_) << "\",";
    os << "\"role\":\"" << (isShadowMode_ ? "SHADOW" : "PRIMARY") << "\",";
    os << "\"level\":\"" << jsonEscapeString(level) << "\",";
    os << "\"event_type\":\"" << jsonEscapeString(eventType) << "\",";
    os << "\"payload\":";
    if (looksLikeJsonValue(jsonPayload)) {
        os << jsonPayload;
    } else {
        os << "\"" << jsonEscapeString(jsonPayload) << "\"";
    }
    os << "}";

    return eventBus_.sendEvent(Event(EventId::TelemetryData, os.str()));
}

void Feature::sleepTelemetryPublishInterval() const {
    sdv::platform::lifecyclemgr::sleepTelemetryPublishInterval(name_);
}

void Feature::log(LogLevel level, const std::string& message) {
    if (!logFile_.is_open()) return;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto tm = std::localtime(&time);

    const char* levelStr = "?";
    switch (level) {
        case LogLevel::info:  levelStr = "INFO "; break;
        case LogLevel::warn:  levelStr = "WARN "; break;
        case LogLevel::error: levelStr = "ERROR"; break;
    }

    const char* roleStr = isShadowMode_ ? "SHADOW" : "PRIMARY";
    logFile_ << "[" << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << "] "
             << "[" << levelStr << "] "
             << "[" << roleStr << "][" << featureDomainTagForLogging() << "] "
             << message << std::endl;
}

const std::string& Feature::getName() const {
    return name_;
}

int64_t Feature::getComponentId() const {
    return componentId_;
}

Flag& Feature::getFlag() {
    return flag_;
}

const Flag& Feature::getFlag() const {
    return flag_;
}

bool Feature::isKilled() const {
    return flag_.isKilled();
}

void Feature::bindShadowToPrimary(const std::string& primaryLogicalName) {
    primaryName_ = primaryLogicalName;
    isShadowMode_ = true;
    flag_.disableShadowMirroring();
}

void Feature::setShadowModeActive(bool active) {
    if (isShadowMode_ || !isPrimaryProcess()) {
        return;
    }
    if (active) {
        flag_.enableShadowMirroring();
    } else {
        flag_.disableShadowMirroring();
    }
    log(LogLevel::info,
        active ? "ShadowMode active (mirroring enabled)"
               : "ShadowMode inactive (mirroring disabled)");
}

bool Feature::tryConsumeAsPlatformControl(const Event& event) {
    const std::string& msg = event.getMessage();

    if (event.getEventId() == EventId::FeatureFlagOff) {
        if (const auto subKey = extractSubFeatureKeyFromMessage(msg, name_)) {
            flag_.setSubFeatureEnabled(*subKey, false);
            log(LogLevel::info, "SubFlag RX: " + msg);
            log(LogLevel::info, "SubFeature " + *subKey + " -> OFF");
            logSubFeatureSnapshotAfterToggle();
            return true;
        }
        if (matchesMainFlagMessage(msg, name_)) {
            log(LogLevel::info, "Flag OFF (by FeatureEvaluator)");
            flag_.disable();
            return true;
        }
    }
    if (event.getEventId() == EventId::FeatureFlagOn) {
        if (const auto subKey = extractSubFeatureKeyFromMessage(msg, name_)) {
            flag_.setSubFeatureEnabled(*subKey, true);
            log(LogLevel::info, "SubFlag RX: " + msg);
            log(LogLevel::info, "SubFeature " + *subKey + " -> ON");
            logSubFeatureSnapshotAfterToggle();
            return true;
        }
        if (matchesMainFlagMessage(msg, name_)) {
            flag_.enable();
            log(LogLevel::info, "Flag ON (by FeatureEvaluator)");
            return true;
        }
    }
    if (event.getEventId() == EventId::KillSwitch) {
        const std::string& msg = event.getMessage();
        // Shadow: broadcast copy of KILL:<Primary> — consume and keep running (no paired stop).
        if (isShadowMode_ && msg == primaryName_) {
            log(LogLevel::info,
                "KillSwitch for Primary " + primaryName_ +
                " (bus broadcast) — Shadow " + name_ +
                " continues; use KILL:" + name_ + " to stop this process");
            return true;
        }
        // Only the named process stops. `KILL:<Primary>_ALL` is two events (Evaluator).
        if (msg != name_) {
            return false;
        }
        log(LogLevel::warn, "Kill switch activated (by FeatureEvaluator) — target: " + name_);
        if (isPrimaryProcess()) {
            flag_.disableShadowMirroring();
            log(LogLevel::info,
                "Primary stopping: mirroring output off (Shadow peer not stopped by this kill)");
        }
        flag_.setKillSwitchLatched(true);
        // Publish a best-effort ACK so external observers (SignalSender/UI) can show progress.
        // The process is about to stop, so do not block on failure.
        (void)eventBus_.sendEvent(Event(EventId::KillSwitchAck, name_ + "|ACK"));
        requestStop();
        return true;
    }
    if (event.getEventId() == EventId::ShadowModeOn &&
        event.getMessage() == primaryName_) {
        if (isPrimaryProcess()) {
            flag_.enableShadowMirroring();
            log(LogLevel::info, "ShadowMode ON (mirroring enabled)");
        }
        return true;
    }
    if (event.getEventId() == EventId::ShadowModeOff &&
        event.getMessage() == primaryName_) {
        if (isPrimaryProcess()) {
            flag_.disableShadowMirroring();
            log(LogLevel::info, "ShadowMode OFF (mirroring disabled)");
        }
        return true;
    }
    if (event.getEventId() == EventId::ShadowPromotionRequest) {
        const std::string& msg = event.getMessage();
        if (isShadowMode_ && msg == primaryName_) {
            log(LogLevel::warn,
                "SHADOW_PROMOTE: Shadow observer exiting — relaunch via LifeCycleMgr as Primary \"" +
                primaryName_ + "\" (hot swap contract)");
            requestStop();
            return true;
        }
        if (!isShadowMode_ && msg == name_) {
            log(LogLevel::warn,
                "SHADOW_PROMOTE: Primary stopping — start paired Shadow binary as new Primary \"" +
                name_ + "\" (orchestrator / LifeCycleMgr)");
            requestStop();
            return true;
        }
        return false;
    }
    if (event.getEventId() == EventId::AbVariantAssign) {
        const std::string& msg = event.getMessage();
        const std::string prefix = name_ + "|";
        if (msg.rfind(prefix, 0) == 0 && msg.size() > prefix.size()) {
            const std::string variant = msg.substr(prefix.size());
            if (!variant.empty()) {
                flag_.setAbVariant(variant);
                log(LogLevel::info, std::string("Variant set to ") + variant);
                return true;
            }
        }
        return false;
    }
    return false;
}

void Feature::logSubFeatureSnapshotAfterToggle() {
    auto st = [this](const char* key) {
        return flag_.isSubFeatureEnabled(key) ? "ON" : "OFF";
    };
    if (name_.find("EvEnergy") != std::string::npos) {
        log(LogLevel::info,
            std::string("EvEnergy sub-feature state (remaining): RANGE=") + st("RANGE") +
            " ECO=" + st("ECO") + " REGEN=" + st("REGEN"));
        return;
    }
    if (name_.find("Adas") != std::string::npos) {
        log(LogLevel::info,
            std::string("Adas sub-feature state (remaining): FCA=") + st("FCA") +
            " LKA=" + st("LKA") + " SCC=" + st("SCC"));
        return;
    }
    if (name_.find("Navigation") != std::string::npos) {
        log(LogLevel::info,
            std::string("Navigation sub-feature state (remaining): LOCATION=") + st("LOCATION") +
            " ROAD=" + st("ROAD") + " GUIDANCE=" + st("GUIDANCE"));
    }
}

std::vector<Event> Feature::pollEvents() {
    EventQueue queue = eventBus_.getEventQueue(componentId_);
    std::vector<Event> appEvents;

    while (queue.getSize() > 0) {
        Event event = queue.getEvent();

        if (tryConsumeAsPlatformControl(event)) {
            // Kill Switch is safety-critical: once it is latched for this process, stop processing
            // any further events immediately (avoid extra logs/actions after kill command).
            if (isStopRequested()) {
                return {};
            }
            continue;
        }
        // If stop is requested (e.g., KillSwitch consumed earlier in this poll), do not forward
        // remaining events to the app layer.
        if (isStopRequested()) {
            return {};
        }
        appEvents.push_back(event);
    }

    return appEvents;
}

bool Feature::sendDomainEvent(EventId id, const std::string& message) {
    if (isShadowMode_ || !isPrimaryProcess()) {
        log(LogLevel::error,
            "Shadow observer / non-Primary: domain send blocked (no vehicle control path)");
        return false;
    }

    // FeatureFlagOff means "do not emit domain output" (process stays alive and keeps consuming control events).
    if (!flag_.isEnabled()) {
        return true;
    }

    if (!eventBus_.sendEvent(Event(id, message))) {
        return false;
    }

    if (!flag_.isShadowMirroringEnabled()) {
        return true;
    }

    std::string payload = primaryName_ + ":" + message;
    std::string mirrorMsg =
        "MIRROR|" + std::to_string(static_cast<int>(id)) + "|" + payload;
    eventBus_.sendEvent(Event(EventId::ShadowMirrorData, mirrorMsg));
    return true;
}

void Feature::requestStop() {
    stopRequested_ = true;
}

bool Feature::isStopRequested() const {
    return stopRequested_ || flag_.isKilled();
}

void Feature::signalHandler(int) {
    stopRequested_ = true;
}

void Feature::applyLaunchAbVariantFromArgv(int argc, char** argv, Flag& flag) {
    if (argc <= 2 || argv == nullptr || argv[2] == nullptr) {
        return;
    }
    std::string m(argv[2]);
    if (m == "A" || m == "B") {
        flag.setAbVariant(m);
    }
}

std::string Feature::featureDomainTagForLogging() const {
    const std::string& ref = isShadowMode_ ? primaryName_ : name_;
    if (ref.find("Adas") != std::string::npos) return "ADAS";
    if (ref.find("Navigation") != std::string::npos) return "NAV";
    if (ref.find("EvEnergy") != std::string::npos) return "ENERGY";
    if (ref.find("WatchDog") != std::string::npos) return "WATCHDOG";
    if (ref.find("FeatureEvaluator") != std::string::npos) return "EVAL";
    if (ref.find("DataCollector") != std::string::npos) return "DATA";
    if (ref.find("SignalReceiver") != std::string::npos) return "SIG_RX";
    if (ref.find("SignalSender") != std::string::npos) return "SIG_TX";
    return "FEATURE";
}

namespace {

using sdv::platform::communications::eventbus::EventId;
using sdv::platform::lifecyclemgr::LogLevel;

static const char* shadowEventIdToString(EventId id) {
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
    case EventId::ShadowPromotionRequest: return "ShadowPromotionRequest";
    case EventId::AbVariantAssign: return "AbVariantAssign";
    case EventId::QuitTheSystem: return "QuitTheSystem";
    default: return "Unknown";
    }
}

static bool tryParseInt32(const std::string& s, int32_t& out) {
    if (s.empty()) return false;
    long long v = 0;
    const char* first = s.data();
    const char* last = s.data() + s.size();
    const auto r = std::from_chars(first, last, v);
    if (r.ec != std::errc{} || r.ptr != last) return false;
    if (v < static_cast<long long>(INT32_MIN) || v > static_cast<long long>(INT32_MAX)) return false;
    out = static_cast<int32_t>(v);
    return true;
}

static bool primaryNameContains(const std::string& primary, const char* needle) {
    return primary.find(needle) != std::string::npos;
}

static int parseIntAfterKey(const std::string& s, const std::string& key) {
    const auto p = s.find(key);
    if (p == std::string::npos) return -1;
    std::size_t i = p + key.size();
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return -1;
    int v = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        v = v * 10 + static_cast<int>(s[i] - '0');
        ++i;
    }
    return v;
}

static int parseNavigationGuidanceStep(const std::string& g) {
    const std::string key = "step=";
    const auto p = g.find(key);
    if (p == std::string::npos) return 1;
    std::size_t i = p + key.size();
    if (i >= g.size() || !std::isdigit(static_cast<unsigned char>(g[i]))) return 1;
    int v = 0;
    while (i < g.size() && std::isdigit(static_cast<unsigned char>(g[i]))) {
        v = v * 10 + static_cast<int>(g[i] - '0');
        ++i;
    }
    return v > 0 ? v : 1;
}

}  // namespace

void Feature::flushNavigationShadowTriplet(const std::string& guidancePayload) {
    const int step = parseNavigationGuidanceStep(guidancePayload);
    log(LogLevel::info,
        "=== " + std::to_string(step) + "회차 정보 브로드캐스트 ===");
    log(LogLevel::info, std::string("차량 위치 전송: ") + navMirrorPendingLoc_);
    log(LogLevel::info, std::string("도로 정보 전송: ") + navMirrorPendingRoad_);
    navMirrorPendingLoc_.clear();
    navMirrorPendingRoad_.clear();
}

void Feature::logEvEnergyShadowEnergyState(const std::string& payload) {
    static constexpr const char kLoc[] = "|SHADOW_LOC|";
    static constexpr const char kRoad[] = "|SHADOW_ROAD|";

    const auto pl = payload.find(kLoc);
    if (pl != std::string::npos) {
        const std::string head = payload.substr(0, pl);
        const std::string detail = payload.substr(pl + sizeof(kLoc) - 1);
        const int battery = parseIntAfterKey(head, "battery=");
        const int rangeKm = parseIntAfterKey(head, "rangeKm=");
        log(LogLevel::info, "차량 위치 수신: " + detail);
        if (battery >= 0) {
            const int rkm = rangeKm >= 0 ? rangeKm : battery * 4;
            log(LogLevel::info,
                "배터리 잔량: " + std::to_string(battery) +
                "% | 예상 주행 가능 거리: " + std::to_string(rkm) + "km");
        }
        return;
    }
    const auto pr = payload.find(kRoad);
    if (pr != std::string::npos) {
        const std::string head = payload.substr(0, pr);
        const std::string road = payload.substr(pr + sizeof(kRoad) - 1);
        const int battery = parseIntAfterKey(head, "battery=");
        log(LogLevel::info, "도로 정보 수신: " + road);
        if (road.find("오르막") != std::string::npos) {
            log(LogLevel::warn, "오르막 감지: 에너지 소모 증가 예상, 에코 모드 권장");
        } else if (road.find("내리막") != std::string::npos) {
            log(LogLevel::info, "내리막 감지: 회생제동 최적화 모드 활성화");
            if (battery >= 0) {
                log(LogLevel::info,
                    "회생제동으로 배터리 회수: " + std::to_string(battery) + "%");
            } else {
                log(LogLevel::info, "회생제동으로 배터리 회수 [미러 동기화]");
            }
        } else {
            log(LogLevel::info, "평지 주행: 표준 에너지 모드 유지");
        }
        return;
    }
    log(LogLevel::info, "에너지 상태 수신: " + payload);
}

void Feature::logMirroredPayloadForShadow(EventId id, const std::string& payload) {
    const std::string& pr = primaryName_;

    if (primaryNameContains(pr, "Adas")) {
        switch (id) {
        case EventId::VehicleLocationUpdate:
            log(LogLevel::info, "현재 위치 수신: " + payload);
            log(LogLevel::info, "주변 차량/장애물 감지 영역 업데이트");
            return;
        case EventId::RoadInfoUpdate:
            log(LogLevel::info, "도로 정보 수신: " + payload);
            log(LogLevel::info, "자율주행 경로 및 속도 계획 반영");
            log(LogLevel::info, "자율주행 중...");
            return;
        case EventId::AdasDecisionUpdate: {
            // Primary mirrors AdasDecisionUpdate with optional "|context" for Shadow replay.
            std::string header = payload;
            std::string detail;
            const auto bar = payload.find('|');
            if (bar != std::string::npos) {
                header = payload.substr(0, bar);
                detail = payload.substr(bar + 1);
            }
            const bool loc = header.find("source=location") != std::string::npos;
            const bool road = header.find("source=road") != std::string::npos;
            if (loc && !detail.empty()) {
                log(LogLevel::info, "현재 위치 수신: " + detail);
                log(LogLevel::info, "주변 차량/장애물 감지 영역 업데이트");
                return;
            }
            if (road && !detail.empty()) {
                log(LogLevel::info, "도로 정보 수신: " + detail);
                log(LogLevel::info, "자율주행 경로 및 속도 계획 반영");
                log(LogLevel::info, "자율주행 중...");
                return;
            }
            log(LogLevel::info, "ADAS 판단 수신: " + payload);
            return;
        }
        default:
            break;
        }
    }

    if (primaryNameContains(pr, "Navigation")) {
        switch (id) {
        case EventId::VehicleLocationUpdate:
            navMirrorPendingLoc_ = payload;
            return;
        case EventId::RoadInfoUpdate:
            navMirrorPendingRoad_ = payload;
            return;
        case EventId::NavigationGuidanceUpdate:
            flushNavigationShadowTriplet(payload);
            return;
        default:
            break;
        }
    }

    if (primaryNameContains(pr, "EvEnergy")) {
        if (id == EventId::EnergyStateUpdate) {
            logEvEnergyShadowEnergyState(payload);
            return;
        }
    }

    switch (id) {
    case EventId::VehicleLocationUpdate:
        log(LogLevel::info, "차량 위치 수신: " + payload);
        return;
    case EventId::RoadInfoUpdate:
        log(LogLevel::info, "도로 정보 수신: " + payload);
        return;
    case EventId::NavigationGuidanceUpdate:
        log(LogLevel::info, "내비게이션 안내 수신: " + payload);
        return;
    case EventId::AdasDecisionUpdate:
        log(LogLevel::info, "ADAS 판단 수신: " + payload);
        return;
    case EventId::EnergyStateUpdate:
        log(LogLevel::info, "에너지 상태 수신: " + payload);
        return;
    default:
        log(LogLevel::warn,
            std::string("미러 수신(미등록 타입): ") + shadowEventIdToString(id) +
            "(" + std::to_string(static_cast<int>(id)) + ") | " + payload);
        return;
    }
}

void Feature::runShadowMirrorObserveLoop() {
    while (!isStopRequested()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto events = pollEvents();
        for (const auto& event : events) {
            if (event.getEventId() != EventId::ShadowMirrorData) continue;

            const std::string& msg = event.getMessage();
            const std::string prefix = "MIRROR|";
            if (msg.rfind(prefix, 0) != 0) continue;

            auto p1 = msg.find('|', prefix.size());
            if (p1 == std::string::npos) continue;

            std::string idStr = msg.substr(prefix.size(), p1 - prefix.size());
            std::string payload = msg.substr(p1 + 1);

            std::string expectedPrefix = primaryName_ + ":";
            if (payload.rfind(expectedPrefix, 0) != 0) {
                continue;
            }
            payload = payload.substr(expectedPrefix.size());

            int32_t originalIdInt = 0;
            if (!tryParseInt32(idStr, originalIdInt)) {
                log(LogLevel::warn, "미러 파싱 실패: 잘못된 원본 이벤트 id: " + idStr);
                continue;
            }

            EventId originalId = static_cast<EventId>(originalIdInt);

            switch (originalId) {
            case EventId::VehicleLocationUpdate:
            case EventId::RoadInfoUpdate:
            case EventId::NavigationGuidanceUpdate:
            case EventId::AdasDecisionUpdate:
            case EventId::EnergyStateUpdate:
                logMirroredPayloadForShadow(originalId, payload);
                break;
            default:
                log(LogLevel::warn,
                    std::string("미러 수신(미등록 타입): UNKNOWN(") +
                    std::to_string(originalIdInt) + ") | " + payload);
                break;
            }
        }
    }
}

}  // namespace sdv::platform::lifecyclemgr

namespace sdv::feature {

ShadowMirrorObserverFeature::ShadowMirrorObserverFeature(
    sdv::platform::communications::eventbus::EventBusInterface& bus,
    const std::string& shadowLogicalName,
    const std::string& primaryLogicalName)
    : sdv::platform::lifecyclemgr::Feature(shadowLogicalName, bus) {
    bindShadowToPrimary(primaryLogicalName);
}

void ShadowMirrorObserverFeature::execute() {}

int runShadowReceiverMain(int argc, char* argv[],
    const char* shadowLogicalName,
    const char* primaryLogicalName) {
    std::string address = "localhost:50051";
    if (argc > 1) {
        address = argv[1];
    }

    signal(SIGTERM, sdv::platform::lifecyclemgr::Feature::signalHandler);
    signal(SIGINT, sdv::platform::lifecyclemgr::Feature::signalHandler);

    sdv::platform::communications::eventbus::EventBusProxy proxy(address);
    ShadowMirrorObserverFeature feature(proxy, shadowLogicalName, primaryLogicalName);
    return feature.run();
}

}  // namespace sdv::feature

#if defined(SDV_SHADOW_ENTRY_MAIN)
int main(int argc, char* argv[]) {
    return sdv::feature::runShadowReceiverMain(argc, argv,
        SDV_SHADOW_LOGICAL_NAME, SDV_SHADOW_PRIMARY_NAME);
}
#endif
#include "LifeCycleManager/Feature.h"

#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

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
#include <optional>
#include <string>

namespace sdv::platform::lifecyclemgr {

namespace {

/// `AdasFeature_A` / `NavigationFeature_B` -> `AdasFeature` / `NavigationFeature` (MQTT UI uses logical names).
std::optional<std::string> carLineLogicalBaseName(const std::string& processName) {
    if (processName.size() < 3) {
        return std::nullopt;
    }
    if (processName[processName.size() - 2] != '_') {
        return std::nullopt;
    }
    const char c = processName.back();
    if (c != 'A' && c != 'B') {
        return std::nullopt;
    }
    return processName.substr(0, processName.size() - 2);
}

bool matchesMainFlagMessage(const std::string& msg,
    const std::string& processName,
    bool shadowObserver,
    bool primaryProcess) {
    if (msg == processName) {
        return true;
    }
    if (shadowObserver || !primaryProcess) {
        return false;
    }
    const auto base = carLineLogicalBaseName(processName);
    return base.has_value() && msg == *base;
}

std::optional<std::string> extractSubFeatureKeyFromMessage(const std::string& msg,
    const std::string& processName) {
    const std::string concrete = processName + ".";
    if (msg.rfind(concrete, 0) == 0 && msg.size() > concrete.size()) {
        return msg.substr(concrete.size());
    }
    const auto base = carLineLogicalBaseName(processName);
    if (!base.has_value()) {
        return std::nullopt;
    }
    const std::string logical = *base + ".";
    if (msg.rfind(logical, 0) == 0 && msg.size() > logical.size()) {
        return msg.substr(logical.size());
    }
    return std::nullopt;
}

}  // namespace

std::atomic<bool> Feature::stopRequested_(false);

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

    // Ensure nested log dir exists (logs/ then logs/A|logs/B).
    mkdir("logs", 0755);
    if (logDir != "logs") {
        mkdir(logDir.c_str(), 0755);
    }

    logFilePath_ = logDir + "/" + normalizeFileName(name) + ".log";
    logFile_.open(logFilePath_, std::ios::out | std::ios::trunc);
}

Feature::~Feature() {
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

int Feature::run() {
    flag_.resetRunState();

    componentId_ = eventBus_.registerComponent();
    if (componentId_ <= 0) {
        log(LogLevel::error, "EventBus registration failed. Terminating.");
        return 1;
    }
    if (flag_.isEnabled()) {
        log(LogLevel::info, "started (ID:" + std::to_string(componentId_) +
            ", PID:" + std::to_string(getpid()) +
            ", Flag:ON)");
    } else {
        log(LogLevel::info, "registered (ID:" + std::to_string(componentId_) +
            ", PID:" + std::to_string(getpid()) +
            ") — FLAG_ON 대기 중");
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
            return true;
        }
        if (matchesMainFlagMessage(msg, name_, isShadowMode_, isPrimaryProcess())) {
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
            return true;
        }
        if (matchesMainFlagMessage(msg, name_, isShadowMode_, isPrimaryProcess())) {
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
        if (msg.rfind(prefix, 0) == 0 && msg.size() == prefix.size() + 1) {
            const char v = msg.back();
            if (v == 'A' || v == 'B') {
                flag_.setAbVariant(v);
                log(LogLevel::info, std::string("A/B variant set to ") + v);
                return true;
            }
        }
        return false;
    }
    return false;
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
        flag.setAbVariant(m[0]);
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
    const bool bLine = primaryNameContains(primaryName_, "NavigationFeature_B");
    const int step = parseNavigationGuidanceStep(guidancePayload);
    if (bLine) {
        log(LogLevel::info,
            "=== " + std::to_string(step) + "회차 (B-line 내비) ===");
    } else {
        log(LogLevel::info,
            "=== " + std::to_string(step) + "회차 정보 브로드캐스트 ===");
    }
    log(LogLevel::info, std::string("차량 위치 전송: ") + navMirrorPendingLoc_);
    log(LogLevel::info, std::string("도로 정보 전송: ") + navMirrorPendingRoad_);
    navMirrorPendingLoc_.clear();
    navMirrorPendingRoad_.clear();
}

void Feature::logEvEnergyShadowEnergyState(const std::string& payload) {
    static constexpr const char kLoc[] = "|SHADOW_LOC|";
    static constexpr const char kRoad[] = "|SHADOW_ROAD|";
    const bool genesisLine = primaryNameContains(primaryName_, "EvEnergyFeature_B");

    const auto pl = payload.find(kLoc);
    if (pl != std::string::npos) {
        const std::string head = payload.substr(0, pl);
        const std::string detail = payload.substr(pl + sizeof(kLoc) - 1);
        const int battery = parseIntAfterKey(head, "battery=");
        const int rangeKm = parseIntAfterKey(head, "rangeKm=");
        if (genesisLine) {
            log(LogLevel::info, "차량 위치 수신 (Genesis energy): " + detail);
            if (battery >= 0 && rangeKm >= 0) {
                log(LogLevel::info,
                    "배터리 잔량: " + std::to_string(battery) +
                    "% | 예상 주행 가능 거리: " + std::to_string(rangeKm) + "km");
            }
        } else {
            log(LogLevel::info, "차량 위치 수신: " + detail);
            if (battery >= 0) {
                const int rkm = rangeKm >= 0 ? rangeKm : battery * 4;
                log(LogLevel::info,
                    "배터리 잔량: " + std::to_string(battery) +
                    "% | 예상 주행 가능 거리: " + std::to_string(rkm) + "km");
            }
        }
        return;
    }
    const auto pr = payload.find(kRoad);
    if (pr != std::string::npos) {
        const std::string head = payload.substr(0, pr);
        const std::string road = payload.substr(pr + sizeof(kRoad) - 1);
        const int battery = parseIntAfterKey(head, "battery=");
        if (genesisLine) {
            log(LogLevel::info, "도로 정보 수신 (Genesis energy): " + road);
            if (road.find("오르막") != std::string::npos) {
                log(LogLevel::warn, "오르막: B-line 에코+파워 한계 보호");
            } else if (road.find("내리막") != std::string::npos) {
                if (battery >= 0) {
                    log(LogLevel::info,
                        "회생제동 (고출력): " + std::to_string(battery) + "%");
                } else {
                    log(LogLevel::info, "회생제동 (고출력) [미러]");
                }
            } else {
                log(LogLevel::info, "평지: 컴포트 주행 모드");
            }
        } else {
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
        }
        return;
    }
    log(LogLevel::info, "에너지 상태 수신: " + payload);
}

void Feature::logMirroredPayloadForShadow(EventId id, const std::string& payload) {
    const std::string& pr = primaryName_;

    if (primaryNameContains(pr, "Adas")) {
        const bool genesisLine = primaryNameContains(pr, "AdasFeature_B");
        switch (id) {
        case EventId::VehicleLocationUpdate:
            if (genesisLine) {
                log(LogLevel::info, "현재 위치 수신 (Genesis): " + payload);
                log(LogLevel::info, "주변 차량/장애물 감지 — 프리미엄 센서 융합");
            } else {
                log(LogLevel::info, "현재 위치 수신: " + payload);
                log(LogLevel::info, "주변 차량/장애물 감지 영역 업데이트");
            }
            return;
        case EventId::RoadInfoUpdate:
            if (genesisLine) {
                log(LogLevel::info, "도로 정보 수신 (Genesis): " + payload);
            } else {
                log(LogLevel::info, "도로 정보 수신: " + payload);
                log(LogLevel::info, "자율주행 경로 및 속도 계획 반영");
                log(LogLevel::info, "자율주행 중...");
            }
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
                if (genesisLine) {
                    log(LogLevel::info, "현재 위치 수신 (Genesis): " + detail);
                    log(LogLevel::info, "주변 차량/장애물 감지 — 프리미엄 센서 융합");
                } else {
                    log(LogLevel::info, "현재 위치 수신: " + detail);
                    log(LogLevel::info, "주변 차량/장애물 감지 영역 업데이트");
                }
                return;
            }
            if (road && !detail.empty()) {
                if (genesisLine) {
                    log(LogLevel::info, "도로 정보 수신 (Genesis): " + detail);
                } else {
                    log(LogLevel::info, "도로 정보 수신: " + detail);
                    log(LogLevel::info, "자율주행 경로 및 속도 계획 반영");
                    log(LogLevel::info, "자율주행 중...");
                }
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
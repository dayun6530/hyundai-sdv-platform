#include "LifeCycleManager/Feature.h"

#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <iomanip>
#include <chrono>
#include <unistd.h>
#include <sys/stat.h>
#include <thread>
#include <signal.h>
#include <cctype>
#include <cstdint>

namespace sdv::platform::lifecyclemgr {

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
    , flag_(name)
    , abAEnabled_(true)
    , abBEnabled_(true) {
    mkdir("logs", 0755);
    logFilePath_ = "logs/" + normalizeFileName(name) + ".log";
    logFile_.open(logFilePath_, std::ios::out | std::ios::trunc);
}

Feature::~Feature() {
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

int Feature::run() {
    flag_.resetRunState();

    if (!flag_.isEnabled()) {
        log(LogLevel::warn, "Flag is OFF. Feature will not start.");
        return 0;
    }

    componentId_ = eventBus_.registerComponent();
    if (componentId_ <= 0) {
        log(LogLevel::error, "EventBus registration failed. Terminating.");
        return 1;
    }
    log(LogLevel::info, "started (ID:" + std::to_string(componentId_) +
        ", PID:" + std::to_string(getpid()) +
        ", Flag:ON)");

    execute();

    eventBus_.unRegister(componentId_);
    log(LogLevel::info, "stopped");
    return 0;
}

void Feature::log(LogLevel level, const std::string& message) {
    if (!logFile_.is_open()) return;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto tm = std::localtime(&time);

    const char* levelStr;
    switch (level) {
        case LogLevel::info:  levelStr = "INFO "; break;
        case LogLevel::warn:  levelStr = "WARN "; break;
        case LogLevel::error: levelStr = "ERROR"; break;
    }

    logFile_ << "[" << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << "] "
             << "[" << levelStr << "] "
             << "[" << name_ << "] "
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
    flag_.disableShadowMirroring();
}

void Feature::setShadowModeActive(bool active) {
    if (!isPrimaryProcess()) {
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
    if (event.getEventId() == EventId::FeatureFlagOff) {
        const std::string& m = event.getMessage();
        if (m == name_) {
            flag_.disable();
            log(LogLevel::info, "Flag OFF (by FeatureEvaluator)");
            return true;
        }
        if (m == name_ + "|A") {
            abAEnabled_ = false;
            log(LogLevel::info, "A/B path A OFF (by FeatureEvaluator)");
            return true;
        }
        if (m == name_ + "|B") {
            abBEnabled_ = false;
            log(LogLevel::info, "A/B path B OFF (by FeatureEvaluator)");
            return true;
        }
        return false;
    }
    if (event.getEventId() == EventId::FeatureFlagOn) {
        const std::string& m = event.getMessage();
        if (m == name_) {
            flag_.enable();
            abAEnabled_ = true;
            abBEnabled_ = true;
            log(LogLevel::info, "Flag ON (by FeatureEvaluator), A/B paths ON");
            return true;
        }
        if (m == name_ + "|A") {
            abAEnabled_ = true;
            log(LogLevel::info, "A/B path A ON (by FeatureEvaluator)");
            return true;
        }
        if (m == name_ + "|B") {
            abBEnabled_ = true;
            log(LogLevel::info, "A/B path B ON (by FeatureEvaluator)");
            return true;
        }
        return false;
    }
    if (event.getEventId() == EventId::KillSwitch) {
        const bool directTarget = (event.getMessage() == name_);
        const bool primaryKilledStopShadow =
            !isPrimaryProcess() && event.getMessage() == primaryName_;
        // `KILL:<Primary>Shadow` stops the Shadow by directTarget; Primary must also exit the pair.
        const bool pairedShadowKilledStopPrimary =
            isPrimaryProcess() && event.getMessage() == (name_ + "Shadow");
        if (!directTarget && !primaryKilledStopShadow && !pairedShadowKilledStopPrimary) {
            return false;
        }
        if (!flag_.isKilled()) {
            if (primaryKilledStopShadow) {
                log(LogLevel::warn,
                    "Kill switch for Primary " + primaryName_ +
                    " — shadow receiver stopping (" + name_ + ")");
            } else if (pairedShadowKilledStopPrimary) {
                log(LogLevel::warn,
                    "Kill switch targets paired Shadow " + event.getMessage() +
                    " — primary stopping (" + name_ + ")");
                flag_.disableShadowMirroring();
                log(LogLevel::info,
                    "Shadow mirroring disabled (Kill Switch — paired Shadow target)");
            } else {
                log(LogLevel::warn, "Kill switch activated (by FeatureEvaluator)");
                if (isPrimaryProcess()) {
                    flag_.disableShadowMirroring();
                    log(LogLevel::info,
                        "Shadow mirroring disabled (Kill Switch — no further ShadowMirrorData from this Primary)");
                }
            }
            flag_.setKilled(true);
            requestStop();
        }
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
    if (event.getEventId() == EventId::AbVariantAssign) {
        const std::string& msg = event.getMessage();
        const std::string prefix = name_ + "|";
        if (msg.rfind(prefix, 0) == 0 && msg.size() == prefix.size() + 1) {
            const char v = msg.back();
            if (v == 'A' || v == 'B') {
                if (v == 'A') {
                    abAEnabled_ = true;
                } else {
                    abBEnabled_ = true;
                }
                flag_.setAbVariant(v);
                log(LogLevel::info,
                    std::string("AB_SET: path ") + v + " ON (by FeatureEvaluator)");
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
            continue;
        }
        appEvents.push_back(event);
    }

    return appEvents;
}

bool Feature::sendDomainEvent(EventId id, const std::string& message) {
    if (!isPrimaryProcess()) {
        log(LogLevel::error, "Shadow process attempted to send domain event. Blocked.");
        return false;
    }

    if (!eventBus_.sendEvent(Event(id, message))) {
        return false;
    }

    if (!flag_.isShadowMirrorEnabled()) {
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

}  // namespace sdv::platform::lifecyclemgr

namespace sdv::feature {

namespace {

using sdv::platform::communications::eventbus::EventBusInterface;
using sdv::platform::communications::eventbus::EventId;
using sdv::platform::lifecyclemgr::Feature;
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
    case EventId::ShadowMirrorData: return "ShadowMirrorData";
    case EventId::ShadowModeOn: return "ShadowModeOn";
    case EventId::ShadowModeOff: return "ShadowModeOff";
    case EventId::QuitTheSystem: return "QuitTheSystem";
    default: return "Unknown";
    }
}

static bool tryParseInt32(const std::string& s, int32_t& out) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-') return false;
    }
    try {
        long v = std::stol(s);
        if (v < INT32_MIN || v > INT32_MAX) return false;
        out = static_cast<int32_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

class ShadowReceiverFeature final : public Feature {
public:
    explicit ShadowReceiverFeature(EventBusInterface& eventBus,
        const std::string& shadowLogicalName,
        const std::string& primaryLogicalName)
        : Feature(shadowLogicalName, eventBus) {
        bindShadowToPrimary(primaryLogicalName);
    }

protected:
    void execute() override {
        log(LogLevel::info, std::string("Shadow started. Primary: ") + primaryName_);

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
                    log(LogLevel::warn, "mirror parse failed: invalid original event id: " + idStr);
                    continue;
                }

                EventId originalId = static_cast<EventId>(originalIdInt);

                switch (originalId) {
                case EventId::VehicleLocationUpdate:
                case EventId::RoadInfoUpdate:
                case EventId::NavigationGuidanceUpdate:
                case EventId::AdasDecisionUpdate:
                case EventId::EnergyStateUpdate:
                    log(LogLevel::info,
                        std::string("mirror received: ") + shadowEventIdToString(originalId) +
                        " payload=[" + payload + "]");
                    break;
                default:
                    log(LogLevel::warn,
                        std::string("mirror received: UNKNOWN(") + std::to_string(originalIdInt) +
                        ") payload=[" + payload + "]");
                    break;
                }
            }
        }
    }
};

}  // namespace

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
    ShadowReceiverFeature feature(proxy, shadowLogicalName, primaryLogicalName);
    return feature.run();
}

}  // namespace sdv::feature
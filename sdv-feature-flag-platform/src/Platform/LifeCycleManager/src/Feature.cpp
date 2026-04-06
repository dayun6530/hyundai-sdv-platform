#include "LifeCycleManager/Feature.h"

#include "FeatureCommunication/EventBus/EventId.h"

#include <iomanip>
#include <chrono>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>

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
    , flag_(name) {
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

std::vector<Event> Feature::pollEvents() {
    EventQueue queue = eventBus_.getEventQueue(componentId_);
    std::vector<Event> appEvents;

    while (queue.getSize() > 0) {
        Event event = queue.getEvent();

        if (event.getEventId() == EventId::FeatureFlagOff &&
            event.getMessage() == name_) {
            flag_.disable();
            log(LogLevel::info, "Flag OFF (by FeatureEvaluator)");
        } else if (event.getEventId() == EventId::FeatureFlagOn &&
                   event.getMessage() == name_) {
            flag_.enable();
            log(LogLevel::info, "Flag ON (by FeatureEvaluator)");
        } else if (event.getEventId() == EventId::KillSwitch &&
                   event.getMessage() == name_) {
            if (!flag_.isKilled()) {
                flag_.setKilled(true);
                log(LogLevel::warn, "Kill switch activated (by FeatureEvaluator)");
                requestStop();
            }
        } else if (event.getEventId() == EventId::ShadowModeOn &&
                   event.getMessage() == primaryName_) {
            if (name_ == primaryName_) {
                flag_.setShadowMirrorEnabled(true);
                log(LogLevel::info, "ShadowMode ON (mirroring enabled)");
            }
        } else if (event.getEventId() == EventId::ShadowModeOff &&
                   event.getMessage() == primaryName_) {
            if (name_ == primaryName_) {
                flag_.setShadowMirrorEnabled(false);
                log(LogLevel::info, "ShadowMode OFF (mirroring disabled)");
            }
        } else if (event.getEventId() == EventId::AbVariantAssign) {
            const std::string& msg = event.getMessage();
            const std::string prefix = name_ + "|";
            if (msg.rfind(prefix, 0) == 0 && msg.size() == prefix.size() + 1) {
                const char v = msg.back();
                if (v == 'A' || v == 'B') {
                    flag_.setAbVariant(v);
                    log(LogLevel::info, std::string("A/B variant set to ") + v);
                    continue;
                }
            }
            appEvents.push_back(event);
        } else {
            appEvents.push_back(event);
        }
    }

    return appEvents;
}

bool Feature::sendDomainEvent(EventId id, const std::string& message) {
    // Hard guard: Shadow processes must never emit domain events.
    if (name_ != primaryName_) {
        log(LogLevel::error, "Shadow process attempted to send domain event. Blocked.");
        return false;
    }

    // Always send the original event.
    if (!eventBus_.sendEvent(Event(id, message))) {
        return false;
    }

    // Shadow mirroring: only Primary mirrors when enabled.
    if (name_ != primaryName_ || !flag_.isShadowMirrorEnabled()) {
        return true;
    }

    // Format: MIRROR|<original_event_id_decimal>|<payload>
    // Payload must not contain '|' (team convention).
    // Also prefix payload with primary logical name so multiple shadows can filter.
    // (Keeps 3 segments: MIRROR|id|payload, and avoids adding extra '|'.)
    std::string payload = primaryName_ + ":" + message;
    std::string mirrorMsg = "MIRROR|" + std::to_string(static_cast<int>(id)) + "|" + payload;
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

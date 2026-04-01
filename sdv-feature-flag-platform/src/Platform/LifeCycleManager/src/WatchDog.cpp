#include "LifeCycleManager/WatchDog.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <thread>
#include <chrono>
#include <signal.h>
#include <string>

namespace sdv::platform::lifecyclemgr {

WatchDog::WatchDog(EventBusInterface& eventBus)
    : Feature("WatchDog", eventBus)
    , lastEventTime_(std::chrono::steady_clock::now())
    , checkIntervalSeconds_(3)
    , timeoutSeconds_(10) {}

void WatchDog::execute() {
    log(LogLevel::info, "monitoring started");

    auto lastCheckTime = std::chrono::steady_clock::now();

    while (!isStopRequested()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto events = pollEvents();
        for (const auto& event : events) {
            analyzeEvent(event);

            if (event.getEventId() == EventId::QuitTheSystem) {
                log(LogLevel::info, "system shutdown detected");
                return;
            }
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - lastCheckTime).count();

        if (elapsed >= checkIntervalSeconds_) {
            checkHealth();
            lastCheckTime = now;
        }
    }
}

void WatchDog::analyzeEvent(const Event& event) {
    lastEventTime_ = std::chrono::steady_clock::now();
    int id = static_cast<int>(event.getEventId());
    eventCounts_[id]++;

    EventId eid = event.getEventId();

    if (eid == EventId::FeatureFlagOn ||
        eid == EventId::FeatureFlagOff ||
        eid == EventId::KillSwitch) {
        logFlagEvent(event);
        return;
    }

    log(LogLevel::info, "event received - ID:" + std::to_string(id) +
        " Msg:[" + event.getMessage() + "]");

    if (eid != EventId::VehicleLocationUpdate &&
        eid != EventId::RoadInfoUpdate &&
        eid != EventId::NavigationGuidanceUpdate &&
        eid != EventId::AdasDecisionUpdate &&
        eid != EventId::EnergyStateUpdate &&
        eid != EventId::FeatureFlagOn &&
        eid != EventId::FeatureFlagOff &&
        eid != EventId::KillSwitch &&
        eid != EventId::ShadowMirrorData &&
        eid != EventId::ShadowModeOn &&
        eid != EventId::ShadowModeOff &&
        eid != EventId::QuitTheSystem) {
        log(LogLevel::warn, "unknown event ID detected: " + std::to_string(id));
    }
}

void WatchDog::logFlagEvent(const Event& event) {
    EventId eid = event.getEventId();
    std::string action;

    if (eid == EventId::FeatureFlagOn) {
        action = "FLAG_ON";
    } else if (eid == EventId::FeatureFlagOff) {
        action = "FLAG_OFF";
    } else if (eid == EventId::KillSwitch) {
        action = "KILL_SWITCH";
    }

    log(LogLevel::info, "[FLAG] " + action + " -> " + event.getMessage());
}

void WatchDog::checkHealth() {
    auto now = std::chrono::steady_clock::now();
    auto silentSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        now - lastEventTime_).count();

    if (silentSeconds > timeoutSeconds_) {
        log(LogLevel::error, "no events for " +
            std::to_string(silentSeconds) + "s - possible system failure");
    } else if (silentSeconds > timeoutSeconds_ / 2) {
        log(LogLevel::warn, "event activity slowing - " +
            std::to_string(silentSeconds) + "s since last event");
    }

    std::string stats = "health check - ";
    int total = 0;
    for (auto& [id, count] : eventCounts_) {
        stats += "ID:" + std::to_string(id) + "=" + std::to_string(count) + " ";
        total += count;
    }
    stats += "(total:" + std::to_string(total) + ")";
    log(LogLevel::info, stats);
}

}  // namespace sdv::platform::lifecyclemgr

int main(int argc, char* argv[]) {
    std::string address = "localhost:50051";
    if (argc > 1) address = argv[1];

    signal(SIGTERM, sdv::platform::lifecyclemgr::Feature::signalHandler);
    signal(SIGINT, sdv::platform::lifecyclemgr::Feature::signalHandler);

    sdv::platform::communications::eventbus::EventBusProxy proxy(address);
    sdv::platform::lifecyclemgr::WatchDog watchDog(proxy);
    return watchDog.run();
}

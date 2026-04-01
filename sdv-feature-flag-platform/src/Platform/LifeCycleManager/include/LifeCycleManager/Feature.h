#pragma once

#include "LifeCycleManager/Flag.h"
#include "FeatureCommunication/EventBus/EventBusInterface.h"
#include "FeatureCommunication/EventBus/Event.h"
#include "FeatureCommunication/EventBus/EventQueue.h"

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>
#include <fstream>

namespace sdv::platform::lifecyclemgr {

using sdv::platform::communications::eventbus::EventBusInterface;
using sdv::platform::communications::eventbus::Event;
using sdv::platform::communications::eventbus::EventQueue;
using sdv::platform::communications::eventbus::EventId;

enum class LogLevel {
    info,
    warn,
    error
};

class Feature {
public:
    Feature(const std::string& name, EventBusInterface& eventBus);
    virtual ~Feature();

    int run();

    const std::string& getName() const;
    int64_t getComponentId() const;
    Flag& getFlag();
    const Flag& getFlag() const;

    static void requestStop();
    static void signalHandler(int signum);

protected:
    virtual void execute() = 0;
    bool isStopRequested() const;
    void log(LogLevel level, const std::string& message);
    std::vector<Event> pollEvents();

    // Send domain event and (optionally) mirror for Shadow Mode.
    // Shadow Mode mirroring is enabled/disabled via EventId::ShadowModeOn/Off.
    bool sendDomainEvent(EventId id, const std::string& message);

    EventBusInterface& eventBus_;
    std::string name_;
    // Primary logical name this process belongs to.
    // - Primary: primaryName_ == name_
    // - Shadow:  primaryName_ != name_ (e.g., NavigationFeatureShadow -> NavigationFeature)
    std::string primaryName_;
    int64_t componentId_;
    Flag flag_;
    bool shadowMirrorEnabled_;

private:
    static std::string normalizeFileName(const std::string& name);

    std::ofstream logFile_;
    std::string logFilePath_;
    static std::atomic<bool> stopRequested_;
};

}  // namespace sdv::platform::lifecyclemgr

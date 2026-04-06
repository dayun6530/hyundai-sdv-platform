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

    /// Kill-switch state lives in \ref flag_.
    bool isKilled() const;

    /// Shadow Mode: when true, Primary processes also emit ShadowMirrorData for each domain event.
    bool isShadowModeActive() const { return flag_.isShadowMirrorEnabled(); }

    static void requestStop();
    static void signalHandler(int signum);

protected:
    virtual void execute() = 0;
    bool isStopRequested() const;
    void log(LogLevel level, const std::string& message);
    std::vector<Event> pollEvents();

    /// True if this process is the Primary (not a Shadow receiver process).
    bool isPrimaryProcess() const { return name_ == primaryName_; }

    /// Attach a Shadow process to its Primary logical name (Team CONVENTIONS: `<Primary>Shadow`).
    void bindShadowToPrimary(const std::string& primaryLogicalName);

    /// Override Shadow Mode without bus events (tests, special orchestration). No-op on Shadow processes.
    void setShadowModeActive(bool active);

    bool sendDomainEvent(EventId id, const std::string& message);

    // A/B 경로는 FLAG_ON/FLAG_OFF 의 세 번째 토큰(A/B) 또는 AB_SET 으로 켜고 끔.
    // 두 경로는 서로 배타적이지 않으며 동시에 활성화할 수 있다.
    bool isAbPathAEnabled() const { return abAEnabled_; }
    bool isAbPathBEnabled() const { return abBEnabled_; }

    EventBusInterface& eventBus_;
    std::string name_;
    std::string primaryName_;
    int64_t componentId_;
    Flag flag_;

private:
    static std::string normalizeFileName(const std::string& name);

    bool tryConsumeAsPlatformControl(const Event& event);

    std::ofstream logFile_;
    std::string logFilePath_;
    static std::atomic<bool> stopRequested_;
    bool abAEnabled_;
    bool abBEnabled_;
};

}  // namespace sdv::platform::lifecyclemgr

namespace sdv::feature {

int runShadowReceiverMain(int argc, char* argv[],
    const char* shadowLogicalName,
    const char* primaryLogicalName);

}  // namespace sdv::feature

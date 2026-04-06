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
    bool isShadowModeActive() const { return flag_.isShadowMirroringEnabled(); }

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

    // Send domain event and (optionally) mirror when Shadow Mode is active (Primary only).
    bool sendDomainEvent(EventId id, const std::string& message);

    // ✅ feature 브랜치 방식 유지 (A/B 동시에 가능)
    bool isAbPathAEnabled() const { return abAEnabled_; }
    bool isAbPathBEnabled() const { return abBEnabled_; }

    EventBusInterface& eventBus_;
    std::string name_;
    std::string primaryName_;
    int64_t componentId_;
    Flag flag_;

private:
    static std::string normalizeFileName(const std::string& name);

    /// Flag/Kill/Shadow On-Off/A-B: returns true if consumed and must not be passed to the app queue.
    bool tryConsumeAsPlatformControl(const Event& event);

    std::ofstream logFile_;
    std::string logFilePath_;
    static std::atomic<bool> stopRequested_;

    // ✅ feature 브랜치 필드 유지
    bool abAEnabled_;
    bool abBEnabled_;
};

}  // namespace sdv::platform::lifecyclemgr

namespace sdv::feature {

/// Entry point for `*Shadow` executables: EventBus connect (optional `argv[1]` host:port),
/// then consume `ShadowMirrorData` for the given Primary name.
int runShadowReceiverMain(int argc, char* argv[],
    const char* shadowLogicalName,
    const char* primaryLogicalName);

}  // namespace sdv::feature
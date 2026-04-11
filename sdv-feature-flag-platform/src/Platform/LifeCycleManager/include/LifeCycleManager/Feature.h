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
#include <optional>

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

/// Platform feature base. Domain subclasses implement \ref execute(); Shadow *observer* processes
/// use \ref bindShadowToPrimary and \ref run() runs the mirror consumer loop instead of \ref execute().
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

    /// Primary-only: bus mirroring of domain output is enabled (ShadowModeOn/Off).
    bool isShadowModeActive() const { return flag_.isShadowMirroringEnabled(); }

    /// True when this process is a Shadow *observer* (no domain \ref sendDomainEvent, mirror log only).
    /// Set by \ref bindShadowToPrimary (used by \ref sdv::feature::ShadowMirrorObserverFeature).
    bool isShadowMode() const { return isShadowMode_; }

    /// Alias for \ref isShadowMode (legacy naming).
    bool isShadowReceiverProcess() const { return isShadowMode_; }

    static void requestStop();
    static void signalHandler(int signum);

    /// Optional \p argv[2] `"A"` or `"B"` from LifeCycleMgr — seeds \ref Flag::setAbVariant before \ref run().
    static void applyLaunchAbVariantFromArgv(int argc, char** argv, Flag& flag);

protected:
    virtual void execute() = 0;
    bool isStopRequested() const;
    void log(LogLevel level, const std::string& message);
    std::vector<Event> pollEvents();

    /// True if this process acts as the Primary logical feature (not a Shadow observer).
    bool isPrimaryProcess() const { return name_ == primaryName_; }

    /// Pairs this observer's logical name with a Primary (enables \ref isShadowMode_, disables mirroring flag).
    void bindShadowToPrimary(const std::string& primaryLogicalName);

    /// Override Shadow Mode without bus events (tests, special orchestration). No-op on Shadow observers.
    void setShadowModeActive(bool active);

    /// Domain output to EventBus. Blocked when \ref isShadowMode() (observer must not take control path).
    bool sendDomainEvent(EventId id, const std::string& message);

    bool isAbPathAEnabled() const { return abAEnabled_; }
    bool isAbPathBEnabled() const { return abBEnabled_; }

    std::optional<char> getAbVariant() const { return flag_.getAbVariant(); }

    /// Short tag for unified logs: `[PRIMARY][ADAS] ...` / `[SHADOW][ADAS] ...`. Override for custom domains.
    virtual std::string featureDomainTagForLogging() const;

    EventBusInterface& eventBus_;
    std::string name_;
    std::string primaryName_;
    int64_t componentId_;
    Flag flag_;

private:
    static std::string normalizeFileName(const std::string& name);

    bool tryConsumeAsPlatformControl(const Event& event);

    void runShadowMirrorObserveLoop();
    void logMirroredPayloadForShadow(EventId id, const std::string& payload);
    void flushNavigationShadowTriplet(const std::string& guidancePayload);
    void logEvEnergyShadowEnergyState(const std::string& payload);

    std::ofstream logFile_;
    std::string logFilePath_;
    static std::atomic<bool> stopRequested_;

    bool abAEnabled_ = false;
    bool abBEnabled_ = false;

    bool isShadowMode_ = false;
    std::string navMirrorPendingLoc_;
    std::string navMirrorPendingRoad_;
};

}  // namespace sdv::platform::lifecyclemgr

namespace sdv::feature {

/// Minimal concrete \ref sdv::platform::lifecyclemgr::Feature used only by `*Shadow` executables.
/// \ref run() detects Shadow mode and consumes `ShadowMirrorData` instead of calling \ref execute().
class ShadowMirrorObserverFeature final : public sdv::platform::lifecyclemgr::Feature {
public:
    ShadowMirrorObserverFeature(sdv::platform::communications::eventbus::EventBusInterface& bus,
        const std::string& shadowLogicalName,
        const std::string& primaryLogicalName);

protected:
    void execute() override;
};

int runShadowReceiverMain(int argc, char* argv[],
    const char* shadowLogicalName,
    const char* primaryLogicalName);

}  // namespace sdv::feature

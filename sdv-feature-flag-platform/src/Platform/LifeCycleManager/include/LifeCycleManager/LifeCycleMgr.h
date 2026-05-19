#pragma once

#include <vector>
#include <string>
#include <sys/types.h>

namespace sdv::platform::lifecyclemgr {

/// Multi-process orchestrator. \c SHADOW_PROMOTE (FeatureEvaluator → \ref EventId::ShadowPromotionRequest)
/// notifies Primary and paired Shadow to exit; the operator must **restart** this manager (or spawn
/// binaries) so the former Shadow executable runs with the Primary logical name — true hot swap is
/// out of scope for this prototype.

class LifeCycleMgr {
public:
    LifeCycleMgr();
    ~LifeCycleMgr();

    bool loadConfig(const std::string& configPath);
    void start();
    void monitor();
    void shutdown();

private:
    struct ProcessInfo {
        std::string execName;
        pid_t pid;
        bool alive;
    };

    void cleanLogDirectory();
    /// SIGKILL any other process whose argv0 basename equals \c carInstanceId_ (e.g. another \c car_5
    /// LifeCycle parent). Prevents two managers interleaving stdout into the same log and reviving stale PIDs.
    void killDuplicatePlatformManagers();
    void killExistingProcesses();
    /// Binds the forked PID to this exact config row (avoids name-collision / mismatch bugs).
    void spawnProcess(ProcessInfo& slot);
    void terminateWithTimeout(std::vector<ProcessInfo>& list, const std::string& label);
    std::string resolveExecPath(const std::string& execName);
    /// Drops `*Shadow` rows from \p features_ when fleet `deploy/state` says shadow lane is inactive.
    void removeInactiveShadowFeaturesUsingFleetState();
    /// Runs \c DeploymentMgr apply-pending-native-car for this instance when fleet deploy root + binary exist.
    void applyPendingNativeRolloutBeforeSpawn();

    std::string address_;
    /// Path passed to \ref loadConfig (exported as \c SDV_LIFECYCLE_CONFIG for FeatureEvaluator KILL resolution).
    std::string lifecycleConfigPath_;
    /// Instance identifier used for log/signal file names (e.g., car_1, car_2, ...).
    std::string carInstanceId_;
    /// Platform-wide log root for this instance (e.g., logs/car_1).
    std::string logRootDir_;
    std::vector<ProcessInfo> communications_;
    std::vector<ProcessInfo> features_;
};

}  // namespace sdv::platform::lifecyclemgr

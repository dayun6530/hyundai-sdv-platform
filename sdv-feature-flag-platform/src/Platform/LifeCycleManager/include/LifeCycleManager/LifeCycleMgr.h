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
    void killExistingProcesses();
    void spawnProcess(const std::string& execName);
    void terminateWithTimeout(std::vector<ProcessInfo>& list, const std::string& label);
    std::string resolveExecPath(const std::string& execName);

    std::string address_;
    /// Path passed to \ref loadConfig (exported as \c SDV_LIFECYCLE_CONFIG for FeatureEvaluator KILL resolution).
    std::string lifecycleConfigPath_;
    /// Instance identifier used for log/signal file names (e.g., car_1, car_2, ...).
    std::string carInstanceId_;
    /// Optional argv[2] `A`/`B` for processes whose exec name has no `Feature_A`/`Feature_B` (car-line names infer A|B).
    std::string launchAbVariant_;
    /// Platform-wide log root for this instance (e.g., logs/A or logs/B).
    std::string logRootDir_;
    std::vector<ProcessInfo> communications_;
    std::vector<ProcessInfo> features_;
};

}  // namespace sdv::platform::lifecyclemgr

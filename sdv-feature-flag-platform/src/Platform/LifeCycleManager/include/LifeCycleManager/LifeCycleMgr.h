#pragma once

#include <vector>
#include <string>
#include <sys/types.h>

namespace sdv::platform::lifecyclemgr {

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
    /// Optional argv[2] `A`/`B` for processes whose exec name has no `Feature_A`/`Feature_B` (car-line names infer A|B).
    std::string launchAbVariant_;
    /// Platform-wide log root for this instance (e.g., logs/A or logs/B).
    std::string logRootDir_;
    std::vector<ProcessInfo> communications_;
    std::vector<ProcessInfo> features_;
};

}  // namespace sdv::platform::lifecyclemgr

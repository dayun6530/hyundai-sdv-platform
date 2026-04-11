#include "LifeCycleManager/LifeCycleMgr.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <linux/limits.h>

namespace sdv::platform::lifecyclemgr {

namespace {

/// Car-line binaries use `Feature_A` / `Feature_B` in the exec name; others use \p configAb only.
std::string resolveAbArgvForExec(const std::string& execName,
                                 const std::string& configAb) {
    if (execName.find("Feature_B") != std::string::npos) {
        return "B";
    }
    if (execName.find("Feature_A") != std::string::npos) {
        return "A";
    }
    return configAb;
}

/// First argv of /proc/<pid>/cmdline, basename only (matches \p execName from config spawn list).
std::string cmdlineArgv0BaseName(const std::string& cmdlineFirstArg) {
    if (cmdlineFirstArg.empty()) {
        return {};
    }
    std::string s = cmdlineFirstArg;
    const std::size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) {
        s = s.substr(slash + 1);
    }
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".exe") == 0) {
        s.resize(s.size() - 4);
    }
    return s;
}

}  // namespace

LifeCycleMgr::LifeCycleMgr()
    : address_("0.0.0.0:50051")
    , logRootDir_("logs") {}

LifeCycleMgr::~LifeCycleMgr() {
    shutdown();
}

std::string LifeCycleMgr::resolveExecPath(const std::string& execName) {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1) return "./" + execName;
    buf[len] = '\0';
    std::string exePath(buf);
    auto pos = exePath.rfind('/');
    if (pos != std::string::npos) {
        return exePath.substr(0, pos) + "/" + execName;
    }
    return "./" + execName;
}

bool LifeCycleMgr::loadConfig(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "[LifeCycleMgr] Failed to open config: " << configPath << std::endl;
        return false;
    }

    std::cout << "[LifeCycleMgr] Loading config: " << configPath << std::endl;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string key, value;
        if (!(iss >> key)) continue;
        if (key[0] == '#') continue;

        if (!(iss >> value)) {
            std::cerr << "[LifeCycleMgr] Invalid config line: " << line << std::endl;
            continue;
        }

        if (key == "address") {
            address_ = value;
        } else if (key == "ab_mode") {
            if (value == "A" || value == "B") {
                launchAbVariant_ = value;
                std::cout << "[LifeCycleMgr] ab_mode (A|B Flag seed): " << value << std::endl;
            } else {
                std::cerr << "[LifeCycleMgr] Ignoring invalid ab_mode (use A or B): " << value
                          << std::endl;
            }
        } else if (key == "communication") {
            communications_.push_back({value, -1, false});
            std::cout << "[LifeCycleMgr] Communication: " << value << std::endl;
        } else if (key == "feature") {
            features_.push_back({value, -1, false});
            std::cout << "[LifeCycleMgr] Feature: " << value << std::endl;
        }
    }

    if (communications_.empty() && features_.empty()) {
        std::cerr << "[LifeCycleMgr] No processes defined in config." << std::endl;
        return false;
    }

    std::cout << "[LifeCycleMgr] " << communications_.size() << " communication(s), "
              << features_.size() << " feature(s) loaded.\n" << std::endl;

    // Derive per-car log root. This avoids crosstalk/cleanup issues when two platforms run together.
    if (launchAbVariant_ == "A") {
        logRootDir_ = "logs/A";
    } else if (launchAbVariant_ == "B") {
        logRootDir_ = "logs/B";
    } else {
        logRootDir_ = "logs";
    }
    std::cout << "[LifeCycleMgr] Log root: " << logRootDir_ << std::endl;
    return true;
}

void LifeCycleMgr::spawnProcess(const std::string& execName) {
    std::string execPath = resolveExecPath(execName);
    const std::string abArgv = resolveAbArgvForExec(execName, launchAbVariant_);
    // Config uses a listen-friendly address (often 0.0.0.0:PORT). Feature clients must use a
    // connect-friendly address; 0.0.0.0 is not a valid gRPC client target.
    // EventBusHost is the server: keep the config address (0.0.0.0:PORT) so it binds all interfaces.
    // Passing 127.0.0.1 only for the host has caused Connection refused on some setups (WSL / gRPC).
    std::string clientAddress = address_;
    static constexpr const char kAnyAddrPrefix[] = "0.0.0.0:";
    if (clientAddress.rfind(kAnyAddrPrefix, 0) == 0) {
        clientAddress.replace(0, sizeof(kAnyAddrPrefix) - 1, "127.0.0.1:");
    }
    const char* argv1Addr =
        (execName == "EventBusHost") ? address_.c_str() : clientAddress.c_str();

    pid_t pid = fork();
    if (pid == 0) {
        if (!abArgv.empty()) {
            execl(execPath.c_str(), execName.c_str(), argv1Addr, abArgv.c_str(), nullptr);
        } else {
            execl(execPath.c_str(), execName.c_str(), argv1Addr, nullptr);
        }
        std::cerr << "Failed to exec " << execPath << std::endl;
        _exit(1);
    } else if (pid > 0) {
        auto updatePid = [&](std::vector<ProcessInfo>& list) {
            for (auto& p : list) {
                if (p.execName == execName && p.pid == -1) {
                    p.pid = pid;
                    p.alive = true;
                    return true;
                }
            }
            return false;
        };
        if (!updatePid(communications_)) updatePid(features_);

        std::cout << "[LifeCycleMgr] Spawned " << execName
                  << " (PID:" << pid << ")" << std::endl;
    } else {
        std::cerr << "[LifeCycleMgr] fork() failed for " << execName << std::endl;
    }
}

void LifeCycleMgr::killExistingProcesses() {
    pid_t myPid = getpid();

    auto killByName = [myPid](const std::string& name) {
        DIR* procDir = opendir("/proc");
        if (!procDir) return;

        struct dirent* entry;
        while ((entry = readdir(procDir)) != nullptr) {
            char* end;
            long pidVal = strtol(entry->d_name, &end, 10);
            if (*end != '\0' || pidVal <= 0) continue;
            if (static_cast<pid_t>(pidVal) == myPid) continue;

            std::string cmdlinePath = "/proc/" + std::string(entry->d_name) + "/cmdline";
            std::ifstream cmdline(cmdlinePath);
            if (!cmdline.is_open()) continue;

            std::string cmd;
            std::getline(cmdline, cmd, '\0');

            // Exact argv0 basename only. Substring match wrongly kills e.g. AdasFeature_B_Shadow
            // when cleaning up AdasFeature_B (B-line Shadow names contain the Primary name).
            if (cmdlineArgv0BaseName(cmd) == name) {
                pid_t targetPid = static_cast<pid_t>(pidVal);
                std::cout << "[LifeCycleMgr] Killing stale process: " << name
                          << " (PID:" << targetPid << ")" << std::endl;
                kill(targetPid, SIGKILL);
                waitpid(targetPid, nullptr, WNOHANG);
            }
        }
        closedir(procDir);
    };

    std::cout << "[LifeCycleMgr] Checking for stale processes..." << std::endl;
    for (const auto& c : communications_) killByName(c.execName);
    for (const auto& f : features_) killByName(f.execName);
}

void LifeCycleMgr::cleanLogDirectory() {
    DIR* dir = opendir(logRootDir_.c_str());
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        std::string path = logRootDir_ + "/" + name;
        std::remove(path.c_str());
    }
    closedir(dir);
    std::cout << "[LifeCycleMgr] Cleaned " << logRootDir_ << " directory." << std::endl;
}

void LifeCycleMgr::start() {
    // Export per-car log root for all child processes.
    setenv("SDV_LOG_DIR", logRootDir_.c_str(), 1);
    if (launchAbVariant_ == "A") {
        setenv("SDV_EXTERNAL_SIGNALS", "config/ExternalSignals_A.cmd", 1);
    } else if (launchAbVariant_ == "B") {
        setenv("SDV_EXTERNAL_SIGNALS", "config/ExternalSignals_B.cmd", 1);
    }

    // Ensure directories exist: logs/ then logs/A|logs/B.
    mkdir("logs", 0755);
    if (logRootDir_ != "logs") {
        mkdir(logRootDir_.c_str(), 0755);
    }
    cleanLogDirectory();
    killExistingProcesses();

    std::cout << "==========================================" << std::endl;
    std::cout << " SDV Feature Flag Platform" << std::endl;
    std::cout << "==========================================" << std::endl;

    for (auto& c : communications_) {
        spawnProcess(c.execName);
        // EventBusHost must accept gRPC before features register; slow /mnt/c/ exec can need >1s.
        const int pauseSec = (c.execName == "EventBusHost") ? 2 : 1;
        std::this_thread::sleep_for(std::chrono::seconds(pauseSec));
    }

    for (auto& f : features_) {
        spawnProcess(f.execName);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << std::endl;
}

void LifeCycleMgr::monitor() {
    auto checkProcess = [](ProcessInfo& p) -> bool {
        if (!p.alive) return false;
        int status;
        pid_t result = waitpid(p.pid, &status, WNOHANG);
        if (result != 0) p.alive = false;
        return p.alive;
    };

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::cout << "[LifeCycleMgr] --- Process Status ---" << std::endl;

        for (auto& c : communications_) {
            bool wasAlive = c.alive;
            checkProcess(c);

            if (wasAlive && !c.alive) {
                std::cerr << "[LifeCycleMgr]   [comm] " << c.execName
                          << " (PID:" << c.pid << ") died. Restarting..."
                          << std::endl;
                c.pid = -1;
                spawnProcess(c.execName);
            } else {
                std::cout << "[LifeCycleMgr]   [comm] " << c.execName
                          << " (PID:" << c.pid << ") : "
                          << (c.alive ? "running" : "stopped") << std::endl;
            }
        }

        int featureRunning = 0;
        for (auto& f : features_) {
            if (checkProcess(f)) featureRunning++;
            std::cout << "[LifeCycleMgr]   [feat] " << f.execName
                      << " (PID:" << f.pid << ") : "
                      << (f.alive ? "running" : "stopped") << std::endl;
        }

        if (featureRunning == 0) {
            std::cout << "[LifeCycleMgr] All features stopped." << std::endl;
            break;
        }
    }
}

void LifeCycleMgr::terminateWithTimeout(std::vector<ProcessInfo>& list,
                                        const std::string& label) {
    static constexpr int timeoutSeconds = 5;

    for (auto& p : list) {
        if (p.alive) kill(p.pid, SIGTERM);
    }

    for (auto& p : list) {
        if (!p.alive) continue;

        bool exited = false;
        for (int i = 0; i < timeoutSeconds * 10; ++i) {
            int status;
            pid_t result = waitpid(p.pid, &status, WNOHANG);
            if (result != 0) {
                exited = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!exited) {
            std::cerr << "[LifeCycleMgr] " << label << " " << p.execName
                      << " (PID:" << p.pid << ") not responding, sending SIGKILL"
                      << std::endl;
            kill(p.pid, SIGKILL);
            waitpid(p.pid, nullptr, 0);
        }
        p.alive = false;
    }
    list.clear();
}

void LifeCycleMgr::shutdown() {
    if (communications_.empty() && features_.empty()) return;

    std::cout << "[LifeCycleMgr] Shutting down..." << std::endl;

    terminateWithTimeout(features_, "[feat]");
    terminateWithTimeout(communications_, "[comm]");

    std::cout << "[LifeCycleMgr] Platform shutdown complete." << std::endl;
}

}  // namespace sdv::platform::lifecyclemgr

int main(int argc, char* argv[]) {
    auto basenameOf = [](const char* path) -> std::string {
        if (path == nullptr) return {};
        std::string s(path);
        const std::size_t slash = s.find_last_of("/\\");
        if (slash != std::string::npos) {
            s = s.substr(slash + 1);
        }
        // Handle possible Windows suffix when invoked from mixed environments.
        static constexpr const char* kExe = ".exe";
        if (s.size() > 4 && s.compare(s.size() - 4, 4, kExe) == 0) {
            s.resize(s.size() - 4);
        }
        return s;
    };

    auto resolveDefaultConfigPathFromArgv0 = [&](const char* argv0) -> std::string {
        const std::string exe = basenameOf(argv0);
        if (exe == "car_A") return "config/car_A.conf";
        if (exe == "car_B") return "config/car_B.conf";
        return {};
    };

    // Priority:
    // 1) explicit argv[1] config path
    // 2) infer by executable name (car_A / car_B)
    const std::string configPath = (argc >= 2 && argv != nullptr && argv[1] != nullptr)
        ? std::string(argv[1])
        : resolveDefaultConfigPathFromArgv0((argv != nullptr) ? argv[0] : nullptr);

    if (configPath.empty()) {
        std::cerr << "[Error] Please specify a config file or run as 'car_A' / 'car_B'."
                  << std::endl;
        return 1;
    }

    sdv::platform::lifecyclemgr::LifeCycleMgr mgr;
    if (!mgr.loadConfig(configPath)) {
        std::cerr << "[LifeCycleMgr] Config load failed. Aborting." << std::endl;
        return 1;
    }
    mgr.start();
    mgr.monitor();
    mgr.shutdown();

    return 0;
}

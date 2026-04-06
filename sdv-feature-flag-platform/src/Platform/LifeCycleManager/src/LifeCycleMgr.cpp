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

}  // namespace

LifeCycleMgr::LifeCycleMgr()
    : address_("0.0.0.0:50051") {}

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
    return true;
}

void LifeCycleMgr::spawnProcess(const std::string& execName) {
    std::string execPath = resolveExecPath(execName);
    const std::string abArgv = resolveAbArgvForExec(execName, launchAbVariant_);

    pid_t pid = fork();
    if (pid == 0) {
        if (!abArgv.empty()) {
            execl(execPath.c_str(), execName.c_str(), address_.c_str(),
                abArgv.c_str(), nullptr);
        } else {
            execl(execPath.c_str(), execName.c_str(), address_.c_str(), nullptr);
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

            if (cmd.find(name) != std::string::npos) {
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
    DIR* dir = opendir("logs");
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        std::string path = "logs/" + name;
        std::remove(path.c_str());
    }
    closedir(dir);
    std::cout << "[LifeCycleMgr] Cleaned logs/ directory." << std::endl;
}

void LifeCycleMgr::start() {
    mkdir("logs", 0755);
    cleanLogDirectory();
    killExistingProcesses();

    std::cout << "==========================================" << std::endl;
    std::cout << " SDV Feature Flag Platform" << std::endl;
    std::cout << "==========================================" << std::endl;

    for (auto& c : communications_) {
        spawnProcess(c.execName);
        std::this_thread::sleep_for(std::chrono::seconds(1));
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
    std::string configPath = "config/SdvPlatform.conf";
    if (argc > 1) configPath = argv[1];

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

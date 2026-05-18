#include "LifeCycleManager/LifeCycleMgr.h"
#include "LifeCycleManager/FleetVersionResolve.h"

#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cctype>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <cerrno>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstring>
#include <linux/limits.h>
#include <regex>
#include <unordered_map>
#include <string_view>
#include <filesystem>

#if SDV_LIFECYCLE_HAS_NJSON
#include <nlohmann/json.hpp>
#endif

namespace sdv::platform::lifecyclemgr {

namespace {

std::string trimExecToken(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.erase(s.begin());
    }
    return s;
}

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

#if SDV_LIFECYCLE_HAS_NJSON
std::string normalizeFleetVersionToken(std::string_view in) {
    std::string s;
    s.reserve(in.size());
    for (char c : in) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (s.size() >= 2 && s[0] == 'v' && std::isdigit(static_cast<unsigned char>(s[1]))) {
        s.erase(0, 1);
    }
    return s;
}

std::string deriveFleetShadowMode(bool shadowEnabled, const std::string& activeVer, const std::string& shadowVer) {
    if (!shadowEnabled) {
        return "inactive";
    }
    const std::string sv = normalizeFleetVersionToken(shadowVer);
    if (sv.empty()) {
        return "inactive";
    }
    if (normalizeFleetVersionToken(activeVer) == sv) {
        return "inactive";
    }
    return "candidate";
}
#endif

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

/// Read `/proc/<pid>/cmdline` without iostreams. Processes can vanish mid-scan (ESRCH/EIO); ifstream can
/// throw `std::ios_base::failure` ("No such process") on glibc in that window.
bool readProcCmdlineRaw(pid_t pid, std::string* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    char path[64];
    const int pn = snprintf(path, sizeof(path), "/proc/%d/cmdline", static_cast<int>(pid));
    if (pn <= 0 || static_cast<std::size_t>(pn) >= sizeof(path)) {
        return false;
    }
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    char buf[32768];
    const ssize_t nr = read(fd, buf, sizeof(buf));
    close(fd);
    if (nr <= 0) {
        return false;
    }
    out->assign(buf, static_cast<std::size_t>(nr));
    return true;
}

// Normalize feature exec name to its logical base:
// - NavigationFeature_A / NavigationFeature_B -> NavigationFeature
// - NavigationFeature_A_Shadow -> NavigationFeature
// - AdasFeature_B_Shadow -> AdasFeature
std::string featureLogicalBaseFromExecName(const std::string& execName) {
    std::string s = trimExecToken(execName);
    static constexpr const char* kShadow = "_Shadow";
    if (s.size() > 7 && s.compare(s.size() - 7, 7, kShadow) == 0) {
        s.resize(s.size() - 7);
    }
    if (s.size() > 2 && s[s.size() - 2] == '_' &&
        (s.back() == 'A' || s.back() == 'B')) {
        s.resize(s.size() - 2);
    }
    return s;
}

std::string stripShadowSuffix(const std::string& name) {
    std::string s = trimExecToken(name);
    static constexpr const char* kShadow = "_Shadow";
    if (s.size() > 7 && s.compare(s.size() - 7, 7, kShadow) == 0) {
        s.resize(s.size() - 7);
    }
    return s;
}

bool hasCarLineSuffix(const std::string& primaryName) {
    return primaryName.size() >= 2 && primaryName[primaryName.size() - 2] == '_' &&
        (primaryName.back() == 'A' || primaryName.back() == 'B');
}

/// Match EventBus main-flag message to a spawned feature executable name.
/// Applies to Navigation / Adas / EvEnergy (any `*Feature_A|B` + optional `_Shadow`).
/// - `AdasFeature` (logical) matches `AdasFeature_A`, `AdasFeature_B`, and their `*_Shadow` rows in config.
/// - `AdasFeature_B` matches `AdasFeature_B` and `AdasFeature_B_Shadow` only.
bool featureExecMatchesMainFlagMessage(const std::string& execName, const std::string& msg) {
    const std::string e = trimExecToken(execName);
    const std::string mPrimary = stripShadowSuffix(msg);
    if (e == trimExecToken(msg)) {
        return true;
    }
    if (hasCarLineSuffix(mPrimary)) {
        return stripShadowSuffix(e) == mPrimary;
    }
    return featureLogicalBaseFromExecName(e) == mPrimary;
}

// Convert server bind address (0.0.0.0:PORT) to connect target (127.0.0.1:PORT).
std::string connectFriendlyAddress(std::string addr) {
    static constexpr const char kAnyAddrPrefix[] = "0.0.0.0:";
    if (addr.rfind(kAnyAddrPrefix, 0) == 0) {
        addr.replace(0, sizeof(kAnyAddrPrefix) - 1, "127.0.0.1:");
    }
    return addr;
}

}  // namespace

LifeCycleMgr::LifeCycleMgr()
    : address_("0.0.0.0:50051")
    , carInstanceId_("")
    , logRootDir_("logs") {}

LifeCycleMgr::~LifeCycleMgr() {
    shutdown();
}

std::string LifeCycleMgr::resolveExecPath(const std::string& execName) {
    // Prefer deploy artifacts location:
    // - If SDV_PLATFORM_DEPLOY_ROOT is set, use <root>/active/<execName>
    // - Otherwise, fallback to cwd-relative deploy/active/<execName>
    if (const char* envRoot = std::getenv("SDV_PLATFORM_DEPLOY_ROOT")) {
        if (envRoot[0] != '\0') {
            std::string base(envRoot);
            if (!base.empty() && base.back() != '/') base.push_back('/');
            const std::string deployed = base + "active/" + execName;
            if (access(deployed.c_str(), X_OK) == 0) {
                return deployed;
            }
        }
    }
    const std::string deployed = std::string("deploy/active/") + execName;
    if (access(deployed.c_str(), X_OK) == 0) {
        return deployed;
    }

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

    communications_.clear();
    features_.clear();

    lifecycleConfigPath_ = configPath;
    if (carInstanceId_.empty()) {
        // Derive instance id from config file name if caller didn't set it.
        // config/car/car_1.conf -> car_1.
        const std::size_t slash = configPath.find_last_of("/\\");
        std::string base = (slash == std::string::npos) ? configPath : configPath.substr(slash + 1);
        static constexpr const char* kConf = ".conf";
        if (base.size() > 5 && base.compare(base.size() - 5, 5, kConf) == 0) {
            base.resize(base.size() - 5);
        }
        carInstanceId_ = base;
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
        value = trimExecToken(value);

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

#if SDV_LIFECYCLE_HAS_NJSON
    removeInactiveShadowFeaturesUsingFleetState();
    std::cout << "[LifeCycleMgr] After fleet shadow_mode filter: " << features_.size()
              << " feature(s) will be spawned.\n"
              << std::endl;
#endif

    // Derive per-instance log root. This avoids crosstalk/cleanup issues when multiple platforms run together.
    // Keep backward-compatible fallback if instance id is missing.
    if (!carInstanceId_.empty()) {
        logRootDir_ = "logs/" + carInstanceId_;
    } else if (launchAbVariant_ == "A") {
        logRootDir_ = "logs/A";
    } else if (launchAbVariant_ == "B") {
        logRootDir_ = "logs/B";
    } else {
        logRootDir_ = "logs";
    }
    std::cout << "[LifeCycleMgr] Log root: " << logRootDir_ << std::endl;
    return true;
}

void LifeCycleMgr::removeInactiveShadowFeaturesUsingFleetState() {
#if SDV_LIFECYCLE_HAS_NJSON
    namespace fs = std::filesystem;
    if (carInstanceId_.empty()) {
        return;
    }
    fs::path fleetPath;
    if (const char* p = std::getenv("SDV_FLEET_DEPLOY_STATE_PATH")) {
        if (p[0] != '\0') {
            fleetPath = p;
        }
    }
    // start_platform_wsl.sh sets SDV_PLATFORM_DEPLOY_ROOT → .../sdv-ota-manager/platform/deploy (reliable on WSL).
    if (fleetPath.empty()) {
        if (const char* deployRoot = std::getenv("SDV_PLATFORM_DEPLOY_ROOT")) {
            if (deployRoot[0] != '\0') {
                fleetPath = fs::path(deployRoot) / "state" / (carInstanceId_ + ".json");
            }
        }
    }
    if (fleetPath.empty()) {
        std::error_code ec;
        const fs::path cfgAbs = fs::absolute(fs::path(lifecycleConfigPath_));
        const fs::path cfgCanon = fs::weakly_canonical(cfgAbs, ec);
        const fs::path& cfgBase = ec ? cfgAbs : cfgCanon;
        const fs::path platformRoot = cfgBase.parent_path().parent_path().parent_path();
        fleetPath = platformRoot.parent_path() / "sdv-ota-manager" / "platform" / "deploy" / "state" /
                    (carInstanceId_ + ".json");
    }
    std::error_code ec;
    if (!fs::is_regular_file(fleetPath, ec)) {
        std::cout << "[LifeCycleMgr] Fleet deploy state not found (" << fleetPath.string()
                  << "); omitting native *Shadow spawns (inactive)." << std::endl;
        const auto newEnd = std::remove_if(features_.begin(), features_.end(), [&](const ProcessInfo& pi) {
            const std::string w = trimExecToken(pi.execName);
            const bool isShadow = (w == "AdasFeatureShadow" || w == "NavigationFeatureShadow" ||
                w == "EvEnergyFeatureShadow");
            if (isShadow) {
                std::cout << "[LifeCycleMgr] Skipping " << w << " (no fleet state file)." << std::endl;
            }
            return isShadow;
        });
        features_.erase(newEnd, features_.end());
        return;
    }
    std::ifstream in(fleetPath);
    if (!in) {
        return;
    }
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        std::cerr << "[LifeCycleMgr] Failed to parse fleet state JSON: " << e.what() << std::endl;
        return;
    }
    auto modeFor = [&](const char* fleetKey) -> std::string {
        try {
            const auto& f = j.at("features").at(fleetKey);
            return deriveFleetShadowMode(f.value("shadow_enabled", false), f.value("active_version", std::string{}),
                f.value("shadow_version", std::string{}));
        } catch (...) {
            return "inactive";
        }
    };
    const std::string modeAdas = modeFor("ADAS");
    const std::string modeNav = modeFor("Navigation");
    const std::string modeEnergy = modeFor("Energy");

    auto shadowDomain = [](const std::string& exec) -> const char* {
        const std::string w = trimExecToken(exec);
        if (w == "AdasFeatureShadow") {
            return "ADAS";
        }
        if (w == "NavigationFeatureShadow") {
            return "Navigation";
        }
        if (w == "EvEnergyFeatureShadow") {
            return "Energy";
        }
        return nullptr;
    };

    const auto newEnd = std::remove_if(features_.begin(), features_.end(), [&](const ProcessInfo& pi) {
        const char* d = shadowDomain(pi.execName);
        if (!d) {
            return false;
        }
        std::string m = "inactive";
        if (std::string(d) == "ADAS") {
            m = modeAdas;
        } else if (std::string(d) == "Navigation") {
            m = modeNav;
        } else if (std::string(d) == "Energy") {
            m = modeEnergy;
        }
        if (m == "candidate") {
            return false;
        }
        std::cout << "[LifeCycleMgr] Skipping " << trimExecToken(pi.execName)
                  << " (fleet shadow lane inactive for " << d << ")." << std::endl;
        return true;
    });
    features_.erase(newEnd, features_.end());
#endif
}

void LifeCycleMgr::spawnProcess(ProcessInfo& slot) {
    const std::string want = trimExecToken(slot.execName);

    if (!carInstanceId_.empty() && isFleetFeatureProcessName(want)) {
        std::string gateLine;
        std::string warnLine;
        if (!shouldSpawnFleetProcess(want, carInstanceId_, lifecycleConfigPath_, gateLine, warnLine)) {
            std::cout << gateLine << std::endl;
            appendFleetSpawnGateLog(carInstanceId_, logRootDir_, want, gateLine);
            slot.pid = 0;
            slot.alive = false;
            return;
        }
        if (!warnLine.empty()) {
            std::cout << warnLine << std::endl;
            appendFleetSpawnGateLog(carInstanceId_, logRootDir_, want, warnLine);
        }
    }

    std::string execPath = resolveExecPath(want);
    const std::string abArgv = resolveAbArgvForExec(want, launchAbVariant_);
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
        (want == "EventBusHost") ? address_.c_str() : clientAddress.c_str();

    // Child processes read fleet state for deploy banners (e.g. AdasFeature_v2.0 is deployed!).
    if (!carInstanceId_.empty()) {
        ::setenv("SDV_CAR_INSTANCE", carInstanceId_.c_str(), 1);
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Always append instance id as the last argv to allow safe cleanup without
        // killing other concurrently running car instances.
        if (!carInstanceId_.empty()) {
            if (!abArgv.empty()) {
                execl(execPath.c_str(), want.c_str(), argv1Addr, abArgv.c_str(), carInstanceId_.c_str(), nullptr);
            } else {
                execl(execPath.c_str(), want.c_str(), argv1Addr, carInstanceId_.c_str(), nullptr);
            }
        } else {
            if (!abArgv.empty()) {
                execl(execPath.c_str(), want.c_str(), argv1Addr, abArgv.c_str(), nullptr);
            } else {
                execl(execPath.c_str(), want.c_str(), argv1Addr, nullptr);
            }
        }
        std::cerr << "Failed to exec " << execPath << std::endl;
        _exit(1);
    } else if (pid > 0) {
        slot.pid = pid;
        slot.alive = true;
        std::cout << "[LifeCycleMgr] Spawned " << want << " (PID:" << pid << ")" << std::endl;
        if (!carInstanceId_.empty() && isFleetFeatureProcessName(want)) {
            if (auto info = resolveFleetVersionForProcess(want, carInstanceId_, lifecycleConfigPath_)) {
                const std::string verLine = formatVersionLogLine(*info, carInstanceId_);
                const std::string startedLine = formatFeatureStartedLogLine(*info, carInstanceId_);
                std::cout << verLine << std::endl;
                std::cout << startedLine << std::endl;
                appendRunSdvVersionLog(carInstanceId_, verLine);
                appendRunSdvVersionLog(carInstanceId_, startedLine);
            }
        }
    } else {
        std::cerr << "[LifeCycleMgr] fork() failed for " << want << std::endl;
    }
}

void LifeCycleMgr::killDuplicatePlatformManagers() {
    const std::string& id = carInstanceId_;
    if (id.empty()) {
        return;
    }

    pid_t myPid = getpid();
    DIR* procDir = opendir("/proc");
    if (!procDir) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(procDir)) != nullptr) {
        char* end = nullptr;
        const long pidVal = strtol(entry->d_name, &end, 10);
        if (*end != '\0' || pidVal <= 0) {
            continue;
        }
        const pid_t pid = static_cast<pid_t>(pidVal);
        if (pid == myPid) {
            continue;
        }

        std::string all;
        if (!readProcCmdlineRaw(pid, &all)) {
            continue;
        }

        const std::size_t nul = all.find('\0');
        const std::string argv0 = (nul == std::string::npos) ? all : all.substr(0, nul);
        if (cmdlineArgv0BaseName(argv0) != id) {
            continue;
        }

        std::cout << "[LifeCycleMgr] Killing duplicate platform parent: " << id << " (PID:" << pid << ")"
                  << std::endl;
        kill(pid, SIGKILL);
        (void)waitpid(pid, nullptr, WNOHANG);
    }
    closedir(procDir);
}

void LifeCycleMgr::killExistingProcesses() {
    pid_t myPid = getpid();

    const std::string instance = carInstanceId_;
    if (instance.empty()) {
        std::cout << "[LifeCycleMgr] No car instance id; skip stale process cleanup." << std::endl;
        return;
    }

    auto killByName = [myPid, instance](const std::string& name) {
        DIR* procDir = opendir("/proc");
        if (!procDir) return;

        struct dirent* entry;
        while ((entry = readdir(procDir)) != nullptr) {
            char* end;
            long pidVal = strtol(entry->d_name, &end, 10);
            if (*end != '\0' || pidVal <= 0) continue;
            if (static_cast<pid_t>(pidVal) == myPid) continue;

            std::string all;
            if (!readProcCmdlineRaw(static_cast<pid_t>(pidVal), &all)) {
                continue;
            }

            // argv0: first segment
            const std::size_t nul = all.find('\0');
            const std::string argv0 = (nul == std::string::npos) ? all : all.substr(0, nul);

            // Must match exact exec name
            if (cmdlineArgv0BaseName(argv0) != name) {
                continue;
            }

            // Must contain our instance id as one of argv segments (we append it on spawn).
            bool sameInstance = false;
            std::size_t pos = 0;
            while (pos < all.size()) {
                std::size_t end = all.find('\0', pos);
                if (end == std::string::npos) end = all.size();
                const std::string arg = all.substr(pos, end - pos);
                if (arg == instance) {
                    sameInstance = true;
                    break;
                }
                pos = end + 1;
            }
            if (!sameInstance) {
                continue;
            }

            // Exact argv0 basename only. Substring match wrongly kills e.g. AdasFeature_B_Shadow
            // when cleaning up AdasFeature_B (B-line Shadow names contain the Primary name).
            pid_t targetPid = static_cast<pid_t>(pidVal);
            std::cout << "[LifeCycleMgr] Killing stale process: " << name
                      << " (PID:" << targetPid << ", carId=" << instance << ")" << std::endl;
            kill(targetPid, SIGKILL);
            waitpid(targetPid, nullptr, WNOHANG);
        }
        closedir(procDir);
    };

    std::cout << "[LifeCycleMgr] Checking for stale processes..." << std::endl;
    for (const auto& c : communications_) killByName(c.execName);
    for (const auto& f : features_) killByName(f.execName);
}

void LifeCycleMgr::applyPendingNativeRolloutBeforeSpawn() {
#ifndef _WIN32
    if (carInstanceId_.empty() || carInstanceId_.rfind("car_", 0) != 0) {
        return;
    }
    const char* deployRootEnv = std::getenv("SDV_PLATFORM_DEPLOY_ROOT");
    if (!deployRootEnv || deployRootEnv[0] == '\0') {
        return;
    }

    namespace fs = std::filesystem;
    const fs::path platformRoot = fs::path(deployRootEnv).parent_path();
    std::string dmPath;
    if (const char* e = std::getenv("SDV_DEPLOYMENT_MGR_EXE")) {
        dmPath = e;
    } else {
        char buf[PATH_MAX];
        const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            const fs::path cand = fs::path(buf).parent_path() / "DeploymentMgr";
            if (access(cand.string().c_str(), X_OK) == 0) {
                dmPath = cand.string();
            }
        }
    }
    if (dmPath.empty()) {
        std::cout << "[LifeCycleMgr] apply-pending-native-car skipped (DeploymentMgr not found next to platform "
                     "binary; set SDV_DEPLOYMENT_MGR_EXE)\n";
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[LifeCycleMgr] fork() failed for apply-pending-native-car\n";
        return;
    }
    if (pid == 0) {
        if (chdir(platformRoot.string().c_str()) != 0) {
            _exit(126);
        }
        execl(dmPath.c_str(), "DeploymentMgr", "apply-pending-native-car", carInstanceId_.c_str(),
            static_cast<char*>(nullptr));
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) {
        std::cerr << "[LifeCycleMgr] waitpid apply-pending-native-car: " << std::strerror(errno) << std::endl;
        return;
    }
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) {
        std::cout << "[LifeCycleMgr] apply-pending-native-car " << carInstanceId_ << " completed.\n";
    } else {
        std::cerr << "[LifeCycleMgr] apply-pending-native-car " << carInstanceId_ << " failed (exit "
                  << (WIFEXITED(st) ? WEXITSTATUS(st) : -1) << ")\n";
    }
#endif
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
    if (!lifecycleConfigPath_.empty()) {
        setenv("SDV_LIFECYCLE_CONFIG", lifecycleConfigPath_.c_str(), 1);
    }
    // Export per-car log root for all child processes.
    setenv("SDV_LOG_DIR", logRootDir_.c_str(), 1);
    // External signal file: one per instance to avoid cross-talk.
    if (!carInstanceId_.empty()) {
        const std::string ext = "config/signals/ExternalSignals_" + carInstanceId_ + ".cmd";
        setenv("SDV_EXTERNAL_SIGNALS", ext.c_str(), 1);
    }

    // Ensure directories exist: logs/ then logs/<carId>.
    mkdir("logs", 0755);
    if (logRootDir_ != "logs") {
        mkdir(logRootDir_.c_str(), 0755);
    }
    // Kill stale comm/feature children first while /proc entries are still stable. Killing the duplicate
    // car_N parent first can tear down the whole tree mid-scan and make ifstream-based cmdline reads throw.
    killExistingProcesses();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // Other car_N platform parent(s) (duplicate LifeCycle) — after children so we do not race /proc.
    killDuplicatePlatformManagers();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    cleanLogDirectory();
    applyPendingNativeRolloutBeforeSpawn();
    // Stale cleanup SIGKILLs processes that are not children of this manager; our ProcessInfo rows
    // must never keep pre-kill PIDs. Reset so monitor() only tracks PIDs from this start() run.
    for (auto& c : communications_) {
        c.pid = -1;
        c.alive = false;
    }
    for (auto& f : features_) {
        f.pid = -1;
        f.alive = false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    std::cout << "==========================================" << std::endl;
    std::cout << " SDV Feature Flag Platform" << std::endl;
    std::cout << "==========================================" << std::endl;

    for (auto& c : communications_) {
        spawnProcess(c);
        // EventBusHost must accept gRPC before features register; slow /mnt/c/ exec can need >1s.
        const int pauseSec = (trimExecToken(c.execName) == "EventBusHost") ? 2 : 1;
        std::this_thread::sleep_for(std::chrono::seconds(pauseSec));
    }

    for (auto& f : features_) {
        spawnProcess(f);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << std::endl;
}

void LifeCycleMgr::monitor() {
    auto checkProcess = [](ProcessInfo& p) -> bool {
        if (!p.alive) return false;
        if (p.pid <= 0) {
            p.alive = false;
            return false;
        }
        int status;
        pid_t result = waitpid(p.pid, &status, WNOHANG);
        if (result == 0) {
            // Still running.
            return true;
        }
        if (result > 0) {
            // Child exited (reaped).
            p.alive = false;
            return false;
        }
        // result == -1: waitpid failed (e.g., ECHILD). Avoid false negatives by probing.
        // On some mixed environments, the child relationship can be ambiguous even though the PID is alive.
        if (errno == ECHILD) {
            if (kill(p.pid, 0) == 0) {
                return true;
            }
        }
        p.alive = false;
        return p.alive;
    };

    // Desired feature process state by execName.
    // Default is "enabled": consistent with demo default Flag=ON and historical behavior.
    std::unordered_map<std::string, bool> wantFeatureAlive;
    for (const auto& f : features_) {
        wantFeatureAlive[trimExecToken(f.execName)] = true;
    }

    // Subscribe to platform control events (FLAG_ON/OFF, KILL) so we can stop/start processes,
    // matching the UX expectation: LifeCycleMgr shows stopped and feature logs end on OFF.
    const std::string clientAddress = connectFriendlyAddress(address_);
    sdv::platform::communications::eventbus::EventBusProxy proxy(clientAddress);
    int64_t mgrComponentId = proxy.registerComponent();
    if (mgrComponentId <= 0) {
        std::cerr << "[LifeCycleMgr] EventBus registration failed. Feature flag start/stop disabled."
                  << std::endl;
    } else {
        std::cout << "[LifeCycleMgr] EventBus connected for flag control (ID:" << mgrComponentId
                  << ", addr=" << clientAddress << ")" << std::endl;
    }

    auto applyMainFlagToFeatures = [&](const std::string& flagMessage, bool enabled) {
        for (const auto& f : features_) {
            const std::string exec = trimExecToken(f.execName);
            if (featureExecMatchesMainFlagMessage(exec, flagMessage)) {
                wantFeatureAlive[exec] = enabled;
            }
        }
    };

    auto handleControlEvent = [&](const sdv::platform::communications::eventbus::Event& e) {
        using sdv::platform::communications::eventbus::EventId;
        const auto eid = e.getEventId();
        const std::string msg = e.getMessage();
        // Only main feature toggles affect process lifetime. Sub-feature toggles contain ".".
        const bool isSubToggle = (msg.find('.') != std::string::npos);
        if (isSubToggle) {
            return;
        }

        if (eid == EventId::FeatureFlagOff) {
            applyMainFlagToFeatures(msg, false);
        } else if (eid == EventId::FeatureFlagOn) {
            applyMainFlagToFeatures(msg, true);
        } else if (eid == EventId::KillSwitch) {
            // KillSwitch is safety-critical: latch OFF so we don't auto-restart until a FlagOn arrives.
            applyMainFlagToFeatures(msg, false);
        }
    };

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::cout << "[LifeCycleMgr] --- Process Status ---" << std::endl;

        // Drain control events and apply desired process states.
        if (mgrComponentId > 0) {
            auto q = proxy.getEventQueue(mgrComponentId);
            while (q.getSize() > 0) {
                auto e = q.getEvent();
                handleControlEvent(e);
            }
        }

        for (auto& c : communications_) {
            bool wasAlive = c.alive;
            checkProcess(c);

            if (wasAlive && !c.alive) {
                std::cerr << "[LifeCycleMgr]   [comm] " << c.execName
                          << " (PID:" << c.pid << ") died. Restarting..."
                          << std::endl;
                c.pid = -1;
                spawnProcess(c);
            } else {
                std::cout << "[LifeCycleMgr]   [comm] " << c.execName
                          << " (PID:" << c.pid << ") : "
                          << (c.alive ? "running" : "stopped") << std::endl;
            }
        }

        int featureRunning = 0;
        for (auto& f : features_) {
            checkProcess(f);

            const std::string exec = trimExecToken(f.execName);
            const bool wantAlive = (wantFeatureAlive.find(exec) == wantFeatureAlive.end())
                ? true
                : wantFeatureAlive[exec];

            // Enforce desired state.
            if (!wantAlive && f.alive) {
                std::cout << "[LifeCycleMgr]   [feat] " << exec
                          << " (PID:" << f.pid << ") : stopping (FLAG_OFF)" << std::endl;
                kill(f.pid, SIGTERM);
                // Do not block here; status will flip on next iteration.
            } else if (wantAlive && !f.alive) {
                std::string gateLine;
                std::string warnLine;
                if (shouldSpawnFleetProcess(exec, carInstanceId_, lifecycleConfigPath_, gateLine, warnLine)) {
                    if (!warnLine.empty()) {
                        std::cout << warnLine << std::endl;
                        appendFleetSpawnGateLog(carInstanceId_, logRootDir_, exec, warnLine);
                    }
                    std::cout << "[LifeCycleMgr]   [feat] " << exec
                              << " : starting (FLAG_ON)" << std::endl;
                    f.pid = -1;
                    spawnProcess(f);
                }
            }

            if (f.alive) {
                featureRunning++;
            }
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
        // Generalized: car_1, car_2, ... -> config/car_1.conf, ...
        static const std::regex kCarN("^car_([0-9]+)$");
        std::smatch m;
        if (std::regex_match(exe, m, kCarN) && m.size() == 2) {
            // Group configs live under config/car/
            return "config/car/" + exe + ".conf";
        }
        return {};
    };

    // Priority:
    // 1) explicit argv[1] config path
    // 2) infer by executable name (car_<n>)
    const std::string configPath = (argc >= 2 && argv != nullptr && argv[1] != nullptr)
        ? std::string(argv[1])
        : resolveDefaultConfigPathFromArgv0((argv != nullptr) ? argv[0] : nullptr);

    if (configPath.empty()) {
        std::cerr << "[Error] Please specify a config file or run as 'car_<n>' (e.g., car_1)."
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

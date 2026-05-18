#include "LifeCycleManager/FleetVersionResolve.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace sdv::platform::lifecyclemgr {

namespace {

struct ProcessFleetMap {
    const char* processName;
    const char* fleetLabel;
    const char* configPrefix;
    const char* stateFeatureKey;
    bool shadowProcess;
};

const ProcessFleetMap kProcessMaps[] = {
    {"AdasFeature", "ADAS", "fleet_adas", "ADAS", false},
    {"AdasFeatureShadow", "ADAS", "fleet_adas", "ADAS", true},
    {"NavigationFeature", "Navigation", "fleet_navigation", "Navigation", false},
    {"NavigationFeatureShadow", "Navigation", "fleet_navigation", "Navigation", true},
    {"EvEnergyFeature", "Energy", "fleet_energy", "Energy", false},
    {"EvEnergyFeatureShadow", "Energy", "fleet_energy", "Energy", true},
};

const ProcessFleetMap* mapForProcess(const std::string& processName) {
    for (const auto& m : kProcessMaps) {
        if (processName == m.processName) {
            return &m;
        }
    }
    return nullptr;
}

std::string trimCopy(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    return s.substr(i);
}

std::string normalizeVersionToken(std::string v) {
    v = trimCopy(std::move(v));
    if (v.empty()) {
        return v;
    }
    if (v[0] == 'v' || v[0] == 'V') {
        if (v.size() >= 2 && std::isdigit(static_cast<unsigned char>(v[1]))) {
            return std::string("v") + v.substr(1);
        }
    }
    if (std::isdigit(static_cast<unsigned char>(v[0]))) {
        return std::string("v") + v;
    }
    return v;
}

std::optional<std::string> readQuotedFieldInFeatureBlock(const std::string& json,
    const char* featureKey,
    const char* field) {
    const std::string marker = std::string("\"") + featureKey + "\"";
    const std::size_t start = json.find(marker);
    if (start == std::string::npos) {
        return std::nullopt;
    }
    std::size_t end = std::string::npos;
    if (std::strcmp(featureKey, "ADAS") == 0) {
        end = json.find("\"Navigation\"", start + 1);
    } else if (std::strcmp(featureKey, "Navigation") == 0) {
        end = json.find("\"Energy\"", start + 1);
    } else if (std::strcmp(featureKey, "Energy") == 0) {
        end = json.find("\"ota\"", start + 1);
    }
    if (end == std::string::npos) {
        end = json.size();
    }
    const std::string block = json.substr(start, end - start);
    const std::string key = std::string("\"") + field + "\"";
    const std::size_t k = block.find(key);
    if (k == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t colon = block.find(':', k);
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::size_t i = colon + 1;
    while (i < block.size() && (block[i] == ' ' || block[i] == '\t')) {
        ++i;
    }
    if (i >= block.size() || block[i] != '"') {
        return std::nullopt;
    }
    ++i;
    const std::size_t valStart = i;
    while (i < block.size() && block[i] != '"') {
        ++i;
    }
    if (i <= valStart) {
        return std::nullopt;
    }
    return block.substr(valStart, i - valStart);
}

std::optional<std::string> readQuotedFieldInDomainBlock(const std::string& json,
    const char* domainKey,
    const char* field) {
    const std::string marker = std::string("\"") + domainKey + "\"";
    const std::size_t start = json.find(marker);
    if (start == std::string::npos) {
        return std::nullopt;
    }
    std::size_t end = std::string::npos;
    if (std::strcmp(domainKey, "adas") == 0) {
        end = json.find("\"navigation\"", start + 1);
    } else if (std::strcmp(domainKey, "navigation") == 0) {
        end = json.find("\"evenergy\"", start + 1);
    } else if (std::strcmp(domainKey, "evenergy") == 0) {
        end = std::min(json.size(), start + 8192);
    }
    if (end == std::string::npos) {
        end = json.size();
    }
    const std::string block = json.substr(start, end - start);
    const std::string key = std::string("\"") + field + "\"";
    const std::size_t k = block.find(key);
    if (k == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t colon = block.find(':', k);
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::size_t i = colon + 1;
    while (i < block.size() && (block[i] == ' ' || block[i] == '\t')) {
        ++i;
    }
    if (i >= block.size() || block[i] != '"') {
        return std::nullopt;
    }
    ++i;
    const std::size_t valStart = i;
    while (i < block.size() && block[i] != '"') {
        ++i;
    }
    if (i <= valStart) {
        return std::nullopt;
    }
    return block.substr(valStart, i - valStart);
}

std::string resolveStateJsonPath(const std::string& carId) {
    if (const char* p = std::getenv("SDV_FLEET_DEPLOY_STATE_PATH")) {
        if (p[0] != '\0') {
            return std::string(p);
        }
    }
    if (const char* deployRoot = std::getenv("SDV_PLATFORM_DEPLOY_ROOT")) {
        if (deployRoot[0] != '\0') {
            std::string root(deployRoot);
            while (!root.empty() && (root.back() == '/' || root.back() == '\\')) {
                root.pop_back();
            }
            return root + "/state/" + carId + ".json";
        }
    }
    return std::string("deploy/state/") + carId + ".json";
}

std::string vehicleRuntimeJsonPathForCar(const std::string& carId) {
    if (const char* p = std::getenv("SDV_VEHICLE_RUNTIME_JSON")) {
        if (p[0] != '\0') {
            return std::string(p);
        }
    }
    const std::string rel = std::string("../sdv-ota-manager/deploy/vehicle_runtime/") + carId + ".json";
    if (access(rel.c_str(), R_OK) == 0) {
        return rel;
    }
    return {};
}

std::string readFileText(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

std::optional<std::string> parseConfigFleetField(const std::string& configPath, const std::string& key) {
    std::ifstream in(configPath);
    if (!in) {
        return std::nullopt;
    }
    std::string line;
    const std::string want = key + " ";
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line.rfind(want, 0) == 0) {
            return trimCopy(line.substr(want.size()));
        }
    }
    return std::nullopt;
}

std::string defaultFleetImage(const char* domainKey, const std::string& version) {
    const std::string ver = normalizeVersionToken(version);
    if (ver.empty()) {
        return {};
    }
    return std::string("sdv/") + domainKey + "-feature:" + ver;
}

const char* runtimeDomainKeyForLabel(const char* fleetLabel) {
    if (std::strcmp(fleetLabel, "ADAS") == 0) {
        return "adas";
    }
    if (std::strcmp(fleetLabel, "Navigation") == 0) {
        return "navigation";
    }
    return "evenergy";
}

}  // namespace

bool isFleetFeatureProcessName(const std::string& processName) {
    return mapForProcess(processName) != nullptr;
}

std::optional<FleetDomainRuntimeInfo> resolveFleetVersionForProcess(const std::string& processName,
    const std::string& carId,
    const std::string& lifecycleConfigPath) {
    const ProcessFleetMap* map = mapForProcess(processName);
    if (map == nullptr || carId.empty()) {
        return std::nullopt;
    }

    FleetDomainRuntimeInfo info;
    info.fleet_label = map->fleetLabel;
    info.mode = map->shadowProcess ? "shadow" : "primary";
    info.source = "unknown";

    std::string configPath = lifecycleConfigPath;
    if (configPath.empty()) {
        if (const char* env = std::getenv("SDV_LIFECYCLE_CONFIG")) {
            if (env[0] != '\0') {
                configPath = env;
            }
        }
    }

    bool configHasVersion = false;
    const std::string statePath = resolveStateJsonPath(carId);
    const std::string stateJson = readFileText(statePath);

    if (!configPath.empty()) {
        const std::string prefix = map->configPrefix;
        if (!map->shadowProcess) {
            if (auto v = parseConfigFleetField(configPath, prefix + "_version")) {
                info.version = normalizeVersionToken(*v);
                configHasVersion = !info.version.empty();
            }
            if (auto s = parseConfigFleetField(configPath, prefix + "_slot")) {
                info.slot = trimCopy(*s);
            }
            if (auto m = parseConfigFleetField(configPath, prefix + "_mode")) {
                const std::string mode = trimCopy(*m);
                if (!mode.empty()) {
                    info.mode = mode;
                }
            }
            if (auto img = parseConfigFleetField(configPath, prefix + "_image")) {
                info.image = trimCopy(*img);
            }
            if (auto bp = parseConfigFleetField(configPath, prefix + "_binary_path")) {
                info.binary_path = trimCopy(*bp);
            }
            if (configHasVersion) {
                info.source = "config";
            }
        }
        if (auto e = parseConfigFleetField(configPath, prefix + "_enabled")) {
            info.enabled = trimCopy(*e);
        }
    }

    if (!map->shadowProcess && info.source == "config" && info.artifact_type.empty() && !stateJson.empty()) {
        if (auto at = readQuotedFieldInFeatureBlock(stateJson, map->stateFeatureKey, "artifact_type")) {
            info.artifact_type = trimCopy(*at);
        }
    }

    if (!map->shadowProcess && info.version.empty() && !stateJson.empty()) {
        if (auto v = readQuotedFieldInFeatureBlock(stateJson, map->stateFeatureKey, "active_version")) {
            info.version = normalizeVersionToken(*v);
        }
        if (info.slot.empty()) {
            if (auto s = readQuotedFieldInFeatureBlock(stateJson, map->stateFeatureKey, "active_slot")) {
                info.slot = *s;
            }
        }
        if (info.image.empty()) {
            if (auto img = readQuotedFieldInFeatureBlock(stateJson, map->stateFeatureKey, "primary_image")) {
                info.image = *img;
            }
        }
        if (info.artifact_type.empty()) {
            if (auto at = readQuotedFieldInFeatureBlock(stateJson, map->stateFeatureKey, "artifact_type")) {
                info.artifact_type = trimCopy(*at);
            }
        }
        if (info.package_id.empty()) {
            if (auto pid = readQuotedFieldInFeatureBlock(stateJson, map->stateFeatureKey, "package_id")) {
                info.package_id = trimCopy(*pid);
            }
        }
        if (!info.version.empty() && info.source == "unknown") {
            info.source = "state";
        }
    }

    if (!map->shadowProcess && info.source == "config" && !stateJson.empty()) {
        if (info.package_id.empty()) {
            if (auto pid = readQuotedFieldInFeatureBlock(stateJson, map->stateFeatureKey, "package_id")) {
                info.package_id = trimCopy(*pid);
            }
        }
    }

    if (map->shadowProcess && !stateJson.empty()) {
        if (auto v = readQuotedFieldInFeatureBlock(stateJson, map->stateFeatureKey, "shadow_version")) {
            info.version = normalizeVersionToken(*v);
        }
        if (info.slot.empty()) {
            if (auto s = readQuotedFieldInFeatureBlock(stateJson, map->stateFeatureKey, "shadow_slot")) {
                info.slot = *s;
            }
        }
        if (info.image.empty()) {
            if (auto img = readQuotedFieldInFeatureBlock(stateJson, map->stateFeatureKey, "shadow_image")) {
                info.image = *img;
            }
        }
        if (!info.version.empty()) {
            info.source = "state";
        }
    }

    if (info.version.empty()) {
        const bool allowRuntime = (std::getenv("SDV_VEHICLE_RUNTIME_JSON") != nullptr);
        if (allowRuntime) {
            const std::string rtPath = vehicleRuntimeJsonPathForCar(carId);
            const std::string rtJson = readFileText(rtPath);
            if (!rtJson.empty()) {
                const char* domain = runtimeDomainKeyForLabel(map->fleetLabel);
                const char* field = map->shadowProcess ? "shadow_version" : "primary_version";
                if (auto v = readQuotedFieldInDomainBlock(rtJson, domain, field)) {
                    info.version = normalizeVersionToken(*v);
                    if (!info.version.empty()) {
                        info.source = "runtime";
                    }
                }
            }
        }
    }

    if (info.enabled.empty() && !stateJson.empty()) {
        if (auto flag = readQuotedFieldInFeatureBlock(stateJson, map->stateFeatureKey, "flag")) {
            info.enabled = (*flag == "ON") ? "ON" : "OFF";
        }
        if (auto ks = readQuotedFieldInFeatureBlock(stateJson, map->stateFeatureKey, "kill_switch")) {
            if (*ks == "true" || *ks == "1") {
                info.enabled = "OFF";
            }
        }
    }
    if (info.enabled.empty()) {
        info.enabled = "ON";
    }

    if (info.version.empty()) {
        info.version = "unknown";
    }
    if (info.slot.empty()) {
        info.slot = "?";
    }
    if (info.image.empty() && info.version != "unknown") {
        info.image = defaultFleetImage(runtimeDomainKeyForLabel(map->fleetLabel), info.version);
    }
    if (info.binary_path.empty()) {
        if (std::strcmp(map->processName, "AdasFeature") == 0 ||
            std::strcmp(map->processName, "AdasFeatureShadow") == 0) {
            info.binary_path = "active/AdasFeature";
        } else if (std::strcmp(map->processName, "NavigationFeature") == 0 ||
            std::strcmp(map->processName, "NavigationFeatureShadow") == 0) {
            info.binary_path = "active/NavigationFeature";
        } else {
            info.binary_path = "active/EvEnergyFeature";
        }
    }

    return info;
}

std::string formatVersionLogLine(const FleetDomainRuntimeInfo& info, const std::string& carId) {
    std::ostringstream os;
    os << "[VERSION] car_id=" << carId << " feature=" << info.fleet_label << " version=" << info.version
       << " slot=" << info.slot << " mode=" << info.mode << " enabled=" << info.enabled << " image=" << info.image;
    if (!info.artifact_type.empty()) {
        os << " artifact_type=" << info.artifact_type;
    }
    if (!info.package_id.empty()) {
        os << " package_id=" << info.package_id;
    }
    os << " binary_path=" << info.binary_path << " source=" << info.source;
    return os.str();
}

std::string formatFeatureStartedLogLine(const FleetDomainRuntimeInfo& info, const std::string& carId) {
    std::ostringstream os;
    os << "[" << info.fleet_label << "] started car_id=" << carId << " version=" << info.version << " slot="
       << info.slot << " mode=" << info.mode << " enabled=" << info.enabled << " source=" << info.source;
    return os.str();
}

void appendRunSdvVersionLog(const std::string& carId, const std::string& line) {
    if (carId.empty() || line.empty()) {
        return;
    }
    mkdir("logs", 0755);
    mkdir("logs/run_sdv", 0755);
    const std::string path = "logs/run_sdv/" + carId + ".log";
    std::ofstream out(path, std::ios::out | std::ios::app);
    if (out) {
        out << line << '\n';
    }
}

std::string featureLogBasename(const std::string& processName) {
    std::string result;
    for (char c : processName) {
        if (c != ' ') {
            result.push_back(c);
        }
    }
    return result;
}

bool ignoreFleetEnabledGate() {
    const char* v = std::getenv("SDV_IGNORE_FLEET_ENABLED");
    if (v == nullptr || v[0] == '\0') {
        return false;
    }
    return v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T';
}

bool isExplicitEnabledOff(const std::string& enabled) {
    if (enabled.empty()) {
        return false;
    }
    std::string e = enabled;
    for (char& c : e) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return e == "OFF";
}

std::string formatFleetSpawnSkipLogLine(const FleetDomainRuntimeInfo& info,
    const std::string& carId,
    const std::string& processName) {
    std::ostringstream os;
    os << "[SKIP] car_id=" << carId << " feature=" << info.fleet_label << " process=" << processName
       << " reason=enabled_OFF version=" << info.version << " slot=" << info.slot << " source=" << info.source;
    return os.str();
}

bool shouldSpawnFleetProcess(const std::string& processName,
    const std::string& carId,
    const std::string& lifecycleConfigPath,
    std::string& outLine,
    std::string& outWarnLine) {
    outLine.clear();
    outWarnLine.clear();
    if (!isFleetFeatureProcessName(processName) || carId.empty()) {
        return true;
    }

    const auto info = resolveFleetVersionForProcess(processName, carId, lifecycleConfigPath);
    if (!info) {
        return true;
    }
    if (info->source == "unknown") {
        return true;
    }
    if (!isExplicitEnabledOff(info->enabled)) {
        return true;
    }

    if (ignoreFleetEnabledGate()) {
        outWarnLine = std::string("[WARN] SDV_IGNORE_FLEET_ENABLED=1, ignoring enabled=OFF for ") + info->fleet_label;
        return true;
    }

    outLine = formatFleetSpawnSkipLogLine(*info, carId, processName);
    return false;
}

void appendFleetSpawnGateLog(const std::string& carId,
    const std::string& logRootDir,
    const std::string& processName,
    const std::string& line) {
    if (line.empty()) {
        return;
    }
    appendRunSdvVersionLog(carId, line);
    if (logRootDir.empty()) {
        return;
    }
    mkdir("logs", 0755);
    mkdir(logRootDir.c_str(), 0755);
    const std::string path = logRootDir + "/" + featureLogBasename(processName) + ".log";
    std::ofstream out(path, std::ios::out | std::ios::app);
    if (out) {
        out << line << '\n';
    }
}

}  // namespace sdv::platform::lifecyclemgr

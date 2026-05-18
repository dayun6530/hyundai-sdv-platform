#include "DeploymentManager/DeploymentManager.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <array>
#include <regex>
#include <sstream>
#include <sys/wait.h>
#include <unordered_set>
#include <vector>

namespace sdv::platform::deployment {

// -----------------------------------------------------------------------------
// DeploymentManager (module boundary note)
//
// This module is responsible for preparing on-vehicle deployment artifacts and
// writing deployment state/history under `deploy/`:
//   - package intake/register (under deploy/packages)
//   - validation (demo-level checks)
//   - placement (packages → staging → active + backup)
//   - state update (deploy/state/*.json + selection/config sync)
//   - rollback metadata (deploy/rollback/*)
//   - history/audit (deploy/history/*, deploy/manifest.txt)
//
// Non-responsibility:
//   - It does NOT start/stop/exec feature processes. The platform runtime
//     (LifeCycleManager or vehicle runtime) reads the prepared files/state and
//     performs actual process lifecycle decisions.
// -----------------------------------------------------------------------------

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

std::string toUpperDomain(std::string d) {
    for (char& c : d) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return d;
}

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string hexU64(std::uint64_t v) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(16) << v;
    return toLower(os.str());
}

std::string nowTimestampIso() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream ts;
    ts << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return ts.str();
}

const std::array<const char*, 3> kInventoryDomains = {"ADAS", "NAV", "ENERGY"};

}  // namespace

namespace {

std::string jsonEscLocal(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

std::string generateRollbackIdFleet() {
    static int counter = 0;
    const int n = ++counter;
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream os;
    os << "rollback_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << std::setw(3) << std::setfill('0') << n;
    return os.str();
}

struct RollbackFileGuard {
    std::string path;
    bool committed{false};
    void commit() { committed = true; }
    ~RollbackFileGuard() {
        if (!committed && !path.empty()) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
};

bool writeAtomicTextFile(const std::string& absPath, const std::string& content, std::string& err) {
    err.clear();
    const std::string tmp = absPath + ".tmp";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    {
        std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
        if (!out.is_open()) {
            err = "failed to open temp file " + tmp;
            return false;
        }
        out << content;
    }
    std::filesystem::rename(tmp, absPath, ec);
    if (ec) {
        err = "failed to rename " + tmp + " to " + absPath + ": " + ec.message();
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

std::string buildFleetRollbackMetadataPrettyJson(const std::string& rollback_id,
    const std::string& timestamp,
    const std::string& car_id,
    const std::string& feature,
    const sdv::platform::deployment::StateManager::FeatureState& prev,
    const std::string& target_slot,
    const std::string& target_version,
    bool sync_config_requested,
    bool config_synced,
    const std::string& config_sync_error,
    const std::string& state_file_rel,
    const std::string& config_file_rel,
    const std::string& config_backup_path) {
    std::ostringstream os;
    os << "{\n";
    os << "  \"rollback_id\": \"" << jsonEscLocal(rollback_id) << "\",\n";
    os << "  \"timestamp\": \"" << jsonEscLocal(timestamp) << "\",\n";
    os << "  \"car_id\": \"" << jsonEscLocal(car_id) << "\",\n";
    os << "  \"feature\": \"" << jsonEscLocal(feature) << "\",\n";
    os << "  \"previous_slot\": \"" << jsonEscLocal(prev.active_slot) << "\",\n";
    os << "  \"previous_version\": \"" << jsonEscLocal(prev.active_version) << "\",\n";
    os << "  \"previous_flag\": \"" << jsonEscLocal(prev.flag) << "\",\n";
    os << "  \"previous_shadow_enabled\": " << (prev.shadow_enabled ? "true" : "false") << ",\n";
    os << "  \"previous_shadow_slot\": \"" << jsonEscLocal(prev.shadow_slot) << "\",\n";
    os << "  \"previous_shadow_version\": \"" << jsonEscLocal(prev.shadow_version) << "\",\n";
    os << "  \"previous_kill_switch\": " << (prev.kill_switch ? "true" : "false") << ",\n";
    os << "  \"previous_kill_reason\": \"" << jsonEscLocal(prev.kill_reason) << "\",\n";
    os << "  \"previous_fallback_slot\": \"" << jsonEscLocal(prev.fallback_slot) << "\",\n";
    os << "  \"previous_fallback_version\": \"" << jsonEscLocal(prev.fallback_version) << "\",\n";
    os << "  \"previous_realtime_route_flag\": \"" << jsonEscLocal(prev.realtime_route_flag) << "\",\n";
    os << "  \"previous_charging_recommend_flag\": \"" << jsonEscLocal(prev.charging_recommend_flag) << "\",\n";
    os << "  \"previous_advanced_range_flag\": \"" << jsonEscLocal(prev.advanced_range_flag) << "\",\n";
    os << "  \"target_slot\": \"" << jsonEscLocal(target_slot) << "\",\n";
    os << "  \"target_version\": \"" << jsonEscLocal(target_version) << "\",\n";
    os << "  \"changed_by_action\": \"fleet-deploy\",\n";
    os << "  \"sync_config_requested\": " << (sync_config_requested ? "true" : "false") << ",\n";
    os << "  \"config_sync_requested\": " << (sync_config_requested ? "true" : "false") << ",\n";
    os << "  \"config_synced\": " << (config_synced ? "true" : "false") << ",\n";
    os << "  \"config_sync_error\": \"" << jsonEscLocal(config_sync_error) << "\",\n";
    os << "  \"config_backup_path\": \"" << jsonEscLocal(config_backup_path) << "\",\n";
    os << "  \"state_file\": \"" << jsonEscLocal(state_file_rel) << "\",\n";
    os << "  \"config_file\": \"" << jsonEscLocal(config_file_rel) << "\"\n";
    os << "}\n";
    return os.str();
}

std::string readFileTextBestEffortLocal(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::optional<std::string> parseJsonStringFieldLooseLocal(const std::string& json, const std::string& key) {
    // Demo-only JSON string field extractor (tolerant, not a full JSON parser).
    // Expected pattern: "key": "value"
    try {
        const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
        std::smatch m;
        if (std::regex_search(json, m, re) && m.size() >= 2) {
            return m[1].str();
        }
    } catch (...) {
        // Best-effort: treat as missing
    }
    return std::nullopt;
}

std::string stripLeadingVTokenLocal(std::string v) {
    v = trim(v);
    if (v.size() >= 2 && (v[0] == 'v' || v[0] == 'V') && std::isdigit(static_cast<unsigned char>(v[1]))) {
        return v.substr(1);
    }
    return v;
}

std::string dockerRepoForFleetFeatureLocal(const std::string& feature) {
    const std::string f = toLower(trim(feature));
    if (f == "adas") return "sdv/adas-feature";
    if (f == "navigation" || f == "nav") return "sdv/navigation-feature";
    if (f == "energy" || f == "evenergy" || f == "ev") return "sdv/evenergy-feature";
    return "sdv/" + (f.empty() ? std::string("feature") : f) + "-feature";
}

struct ManifestResolveHints {
    std::string package_id;
    std::string package_path;
    std::string docker_image;
    std::string artifact_type;
};

struct ResolvedPackageManifest {
    std::string abs_path;
    std::string rel_path;
    std::string artifact_type;
    std::string docker_image;
    std::string image_digest;
    std::string checksum_file;
    std::string package_id;
};

struct RegistryPackagePaths {
    std::string package_path;
    std::string manifest_path;
};

std::string resolvePlatformPath(const std::string& platformRoot, const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return p.lexically_normal().generic_string();
    }
    return (std::filesystem::path(platformRoot) / p).lexically_normal().generic_string();
}

void setManifestRelPath(ResolvedPackageManifest& out, const std::string& platformRoot, const std::string& absPath) {
    out.abs_path = absPath;
    std::error_code rec;
    const auto rel =
        std::filesystem::relative(std::filesystem::path(absPath), std::filesystem::path(platformRoot), rec);
    if (!rec) {
        out.rel_path = rel.generic_string();
    } else {
        out.rel_path = absPath;
    }
}

std::optional<ResolvedPackageManifest> tryManifestAtPlatformPath(const std::string& platformRoot,
    const std::string& pathOrDir) {
    if (pathOrDir.empty()) {
        return std::nullopt;
    }
    const std::string abs = resolvePlatformPath(platformRoot, pathOrDir);
    std::error_code ec;
    std::filesystem::path candidate(abs);
    std::filesystem::path manifestPath;
    if (candidate.filename() == "manifest.json") {
        if (std::filesystem::exists(candidate, ec) && !ec) {
            manifestPath = candidate;
        }
    } else if (std::filesystem::is_directory(candidate, ec) && !ec) {
        const auto nested = candidate / "manifest.json";
        if (std::filesystem::exists(nested, ec) && !ec) {
            manifestPath = nested;
        }
    }
    if (manifestPath.empty()) {
        return std::nullopt;
    }
    ResolvedPackageManifest out;
    setManifestRelPath(out, platformRoot, manifestPath.generic_string());
    return out;
}

std::optional<RegistryPackagePaths> lookupPackageInRegistry(const std::string& registryAbs,
    const std::string& package_id) {
    if (package_id.empty()) {
        return std::nullopt;
    }
    const std::string json = readFileTextBestEffortLocal(registryAbs);
    if (json.empty()) {
        return std::nullopt;
    }
    const std::string needle = "\"package_id\": \"" + package_id + "\"";
    const std::size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t start = pos > 80 ? pos - 80 : 0;
    const std::size_t end = std::min(json.size(), pos + 2500);
    const std::string block = json.substr(start, end - start);
    RegistryPackagePaths out;
    if (const auto p = parseJsonStringFieldLooseLocal(block, "package_path")) {
        out.package_path = *p;
    }
    if (const auto m = parseJsonStringFieldLooseLocal(block, "manifest_path")) {
        out.manifest_path = *m;
    }
    if (out.package_path.empty() && out.manifest_path.empty()) {
        return std::nullopt;
    }
    return out;
}

std::optional<ResolvedPackageManifest> resolvePackageManifestByFeatureVersion(const std::string& packagesDir,
    const std::string& platformRoot,
    const std::string& feature,
    const std::string& target_version) {
    const std::string tvRaw = trim(target_version);
    const std::string tvNoV = stripLeadingVTokenLocal(tvRaw);
    const std::vector<std::string> candidates = {
        packagesDir + "/" + feature + "/" + tvRaw + "/manifest.json",
        packagesDir + "/" + feature + "/v" + tvNoV + "/manifest.json",
        packagesDir + "/" + feature + "/" + tvNoV + "/manifest.json",
    };
    for (const auto& abs : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(abs, ec) || ec) {
            continue;
        }
        ResolvedPackageManifest out;
        setManifestRelPath(out, platformRoot, abs);
        return out;
    }
    return std::nullopt;
}

std::optional<ResolvedPackageManifest> resolvePackageManifest(const std::string& packagesDir,
    const std::string& platformRoot,
    const std::string& feature,
    const std::string& target_version,
    const ManifestResolveHints* hints) {
    if (hints != nullptr) {
        if (!hints->package_path.empty()) {
            if (const auto fromPath = tryManifestAtPlatformPath(platformRoot, hints->package_path)) {
                return fromPath;
            }
        }
        if (!hints->package_id.empty()) {
            const std::string registryAbs = platformRoot + "/deploy/package_registry.json";
            if (const auto reg = lookupPackageInRegistry(registryAbs, hints->package_id)) {
                if (!reg->manifest_path.empty()) {
                    if (const auto fromManifest = tryManifestAtPlatformPath(platformRoot, reg->manifest_path)) {
                        return fromManifest;
                    }
                }
                if (!reg->package_path.empty()) {
                    if (const auto fromRegPath = tryManifestAtPlatformPath(platformRoot, reg->package_path)) {
                        return fromRegPath;
                    }
                }
            }
        }
    }
    return resolvePackageManifestByFeatureVersion(packagesDir, platformRoot, feature, target_version);
}

void parsePackageManifestFields(const std::string& manifestJson, ResolvedPackageManifest& out) {
    out.artifact_type =
        parseJsonStringFieldLooseLocal(manifestJson, "artifact_type")
            .value_or(parseJsonStringFieldLooseLocal(manifestJson, "package_type").value_or(std::string()));
    out.docker_image = parseJsonStringFieldLooseLocal(manifestJson, "docker_image").value_or("");
    out.image_digest = parseJsonStringFieldLooseLocal(manifestJson, "image_digest").value_or("");
    out.checksum_file = parseJsonStringFieldLooseLocal(manifestJson, "checksum_file").value_or("");
    out.package_id = parseJsonStringFieldLooseLocal(manifestJson, "package_id").value_or("");
}

StateManager::FeaturePackageMetadata featureMetadataFromResolved(const ResolvedPackageManifest& m,
    const ManifestResolveHints& hints) {
    StateManager::FeaturePackageMetadata meta;
    meta.primary_image = !m.docker_image.empty() ? m.docker_image : hints.docker_image;
    meta.artifact_type = !m.artifact_type.empty() ? m.artifact_type : hints.artifact_type;
    meta.image_digest = m.image_digest;
    meta.manifest_path = m.rel_path;
    meta.package_id = !hints.package_id.empty() ? hints.package_id : m.package_id;
    return meta;
}

ManifestResolveHints manifestHintsFromVersionInfo(const VersionManager::VersionInfo& info) {
    ManifestResolveHints hints;
    hints.package_id = info.package_id;
    hints.package_path = info.package_path;
    hints.docker_image = info.docker_image;
    hints.artifact_type = info.artifact_type;
    return hints;
}

DeploymentManager::FleetDockerRuntimeOptions fleetDockerRuntimeOrDefault(
    const DeploymentManager::FleetDockerRuntimeOptions* opts) {
    if (opts != nullptr) {
        return *opts;
    }
    return DeploymentManager::FleetDockerRuntimeOptions{};
}

std::string bashSingleQuoteLocal(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out += "'";
    return out;
}

std::optional<std::string> findOtaVehicleScriptPath(const std::string& platformRoot) {
    const std::vector<std::string> candidates = {
        platformRoot + "/../scripts/ota_vehicle.sh",
        platformRoot + "/../../scripts/ota_vehicle.sh",
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return std::filesystem::path(candidate).lexically_normal().generic_string();
        }
    }
    return std::nullopt;
}

std::optional<std::string> fleetFeatureToOtaVehicleDomain(const std::string& feature) {
    if (feature == "ADAS") {
        return std::string("ADAS");
    }
    if (feature == "Navigation") {
        return std::string("Navigation");
    }
    if (feature == "Energy") {
        return std::string("EvEnergy");
    }
    return std::nullopt;
}

std::string fleetSlotRoleLabel(const std::string& slot) {
    if (slot == "B") {
        return "candidate";
    }
    if (slot == "A") {
        return "primary";
    }
    return slot;
}

std::string candidateShadowContainerForCarFeature(const std::string& car_id, const std::string& feature) {
    const auto domain = fleetFeatureToOtaVehicleDomain(feature);
    if (!domain) {
        return {};
    }
    std::string domLower = toLower(*domain);
    return car_id + "_agent_" + domLower + "_shadow";
}

std::string primaryContainerForCarFeature(const std::string& car_id, const std::string& feature) {
    const auto domain = fleetFeatureToOtaVehicleDomain(feature);
    if (!domain) {
        return {};
    }
    return car_id + "_agent_" + toLower(*domain);
}

struct ShellCaptureResult {
    int exit_code{-1};
    std::string output;
};

ShellCaptureResult runShellCapture(const std::string& command) {
    ShellCaptureResult out;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        out.exit_code = -1;
        out.output = "popen failed";
        return out;
    }
    std::array<char, 4096> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        out.output.append(buf.data());
    }
    const int status = pclose(pipe);
    if (status == -1) {
        out.exit_code = -1;
    } else if (WIFEXITED(status)) {
        out.exit_code = WEXITSTATUS(status);
    } else {
        out.exit_code = -1;
    }
    return out;
}

std::string summarizeShellOutput(const std::string& output, std::size_t maxLen = 400) {
    std::string s = trim(output);
    for (char& c : s) {
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }
    }
    if (s.size() > maxLen) {
        s.resize(maxLen);
        s += "...";
    }
    return s;
}

bool dockerContainerNameExists(const std::string& containerName) {
    if (containerName.empty()) {
        return false;
    }
    const std::string cmd = std::string("docker ps -a --format '{{.Names}}' 2>/dev/null | grep -Fxq ")
        + bashSingleQuoteLocal(containerName);
    return runShellCapture(cmd).exit_code == 0;
}

bool looksLikeRegistryAuthFailure(const std::string& reason) {
    const std::string r = toLower(reason);
    return r.find("unauthorized") != std::string::npos || r.find("authentication required") != std::string::npos
        || r.find("denied") != std::string::npos || r.find("incorrect username") != std::string::npos
        || r.find("no basic auth") != std::string::npos;
}

void emitDockerRuntimeFailureHints(const std::string& reason, const std::string& imageRef) {
    if (reason.find("daemon not running") != std::string::npos || reason.find("docker ps") != std::string::npos
        || reason.find("데몬") != std::string::npos || reason.find("daemon") != std::string::npos) {
        std::cout << "[DOCKER_RUNTIME] hint=\"Open Docker Desktop and check docker ps in WSL\"\n";
        return;
    }
    if (reason.find("Docker pull failed") != std::string::npos || looksLikeRegistryAuthFailure(reason)) {
        if (!imageRef.empty()) {
            std::cout << "[DOCKER_RUNTIME] image_ref=" << imageRef << "\n";
        }
        std::cout << "[DOCKER_RUNTIME] hint=\"Pull failed. If this is a private registry, run docker login first "
                     "(e.g. docker login, docker login ghcr.io, docker login registry.example.com)\"\n";
        return;
    }
    if (reason.find("not found locally") != std::string::npos || reason.find("이미지가 없습니다") != std::string::npos) {
        std::cout << "[DOCKER_RUNTIME] hint=\"Build the image locally, push to a registry, or use --docker-pull=missing\"\n";
    }
}

}  // namespace

DeploymentManager::DeploymentManager(std::string workingDirectory)
    : history_(workingDirectory + "/deploy/history")
    , workDir_(std::move(workingDirectory))
    , packagesDir_(workDir_ + "/deploy/packages")
    , stagingDir_(workDir_ + "/deploy/staging")
    , activeDir_(workDir_ + "/deploy/active")
    , backupDir_(workDir_ + "/deploy/backup")
    , metadataDir_(workDir_ + "/deploy/metadata")
    , selectionPath_(workDir_ + "/deploy/active_selection.conf")
    , stacksDir_(workDir_ + "/deploy/stacks")
    , manifestPath_(workDir_ + "/deploy/manifest.txt") {
    if (!ensureDirs()) {
        std::cerr << "[DeploymentManager] Failed to initialize deploy directories under "
                  << workDir_ << "/deploy\n";
    }
}

bool DeploymentManager::isValidFleetFeature(const std::string& feature) {
    return feature == "ADAS" || feature == "Navigation" || feature == "Energy";
}

bool DeploymentManager::isValidFleetSlot(const std::string& slot) {
    return slot == "A" || slot == "B";
}

bool DeploymentManager::checkOtaSafety(const StateManager::VehicleState& s, std::string& reason) {
    if (s.status != "online") {
        reason = "vehicle is not online";
        return false;
    }
    if (s.ota.vehicle_mode != "PARKED") {
        reason = "vehicle is not parked";
        return false;
    }
    if (s.ota.ignition != "OFF") {
        reason = "ignition is not OFF";
        return false;
    }
    // Final SDV/OTA safety gate: updates must only start when the vehicle reports READY.
    // NOTE: `deployToVehicleInternal` intentionally simulates a full OTA lifecycle in one call for demo purposes
    // (DOWNLOADING → READY_TO_INSTALL → INSTALLING → COMPLETED). A production system would split these steps
    // across time/events and only advance when each phase's safety checks pass.
    if (s.ota.update_mode != "READY") {
        reason = "update_mode is not READY";
        return false;
    }
    if (s.ota.gear != "P") {
        reason = "gear is not P";
        return false;
    }
    if (s.ota.speed != 0) {
        reason = "speed is not zero";
        return false;
    }
    if (s.ota.parking_brake != "ON") {
        reason = "parking brake is not ON";
        return false;
    }
    if (s.ota.battery < 30) {
        reason = "battery too low";
        return false;
    }
    if (!s.ota.driver_approved) {
        reason = "driver approval required";
        return false;
    }
    if (s.ota.network != "CONNECTED") {
        reason = "network not connected";
        return false;
    }
    if (s.ota.target_system_running) {
        reason = "target system is running";
        return false;
    }
    return true;
}

bool DeploymentManager::validateTargetVersion(const VersionManager& vm, const std::string& feature,
    const std::string& slot, const std::string& version, std::string& reason) const {
    reason.clear();
    if (!isValidFleetFeature(feature)) {
        reason = "unsupported feature: " + feature;
        return false;
    }
    if (!isValidFleetSlot(slot)) {
        reason = "invalid slot: " + slot;
        return false;
    }
    if (version.empty()) {
        reason = "version is empty";
        return false;
    }
    std::string err;
    const auto info = vm.getInfo(feature, slot[0], err);
    if (!info) {
        reason = err.empty() ? ("missing mapping for " + feature + " " + slot) : err;
        return false;
    }
    if (info->version != version) {
        reason = "version not allowed for slot (expected " + info->version + ", got " + version + ")";
        return false;
    }
    return true;
}

bool DeploymentManager::deployToVehicle(const std::string& car_id, const std::string& feature,
    const std::string& target_slot, const std::string& target_version, std::string& outMessage) {
    outMessage.clear();
    const auto r = deployToVehicleInternal(car_id, feature, target_slot, target_version, false, false);
    outMessage = r.success ? "deploy success" : r.reason;
    return r.success;
}

DeploymentManager::DeployResult DeploymentManager::deployToVehicleInternal(const std::string& car_id,
    const std::string& feature,
    const std::string& target_slot,
    const std::string& target_version,
    bool sync_config_requested,
    bool defer_native_until_restart) {
    DeployResult r;
    r.car_id = car_id;
    r.feature = feature;
    r.target_slot = target_slot;
    r.target_version = target_version;

    RollbackFileGuard rb;

    auto recordOutcome = [&](const std::string& result, const std::string& reason) {
        StateManager rec(workDir_);
        std::string recErr;
        (void)rec.recordLastDeploy(car_id, feature, target_slot, target_version, result, reason, recErr);
    };

    auto fail = [&](const std::string& reason) -> DeployResult {
        if (isValidFleetFeature(feature) && isValidFleetSlot(target_slot)) {
            recordOutcome("failed", reason);
        }
        r.success = false;
        r.reason = reason;
        std::cout << "[DEPLOY] target=" << car_id << " feature=" << feature << " slot=" << target_slot
                  << " version=" << target_version << " result=FAIL reason=" << reason << "\n";
        return r;
    };

    if (!isValidFleetFeature(feature)) {
        return fail("deploy failed: unsupported feature: " + feature);
    }
    if (!isValidFleetSlot(target_slot)) {
        return fail("deploy failed: invalid slot: " + target_slot);
    }

    std::string err;
    const auto registryOpt = VehicleRegistry::loadFromFile(workDir_ + "/config/fleet/vehicles.json", err);
    if (!registryOpt) {
        return fail(err.empty() ? "deploy failed: unable to load vehicles.json" : err);
    }
    const auto vmOpt = VersionManager::loadFromFile(workDir_ + "/deploy/version_map.json", err);
    if (!vmOpt) {
        return fail(err.empty() ? "deploy failed: unable to load version_map.json" : err);
    }
    const auto vMeta = registryOpt->getByCarId(car_id);
    if (!vMeta) {
        return fail("deploy failed: unknown car_id: " + car_id);
    }

    StateManager sm(workDir_);
    auto stateOpt = sm.loadState(car_id, err);
    if (!stateOpt) {
        return fail(err.empty() ? ("deploy failed: unable to load state for " + car_id) : err);
    }
    if (stateOpt->car_id != vMeta->car_id) {
        return fail("deploy failed: state metadata mismatch");
    }

    // Kill-switch gating (force option can be added later). Must not change ota when kill-switch is enabled.
    const auto* fs = feature == "ADAS" ? &stateOpt->adas : (feature == "Navigation" ? &stateOpt->navigation : &stateOpt->energy);
    if (fs->kill_switch) {
        return fail("deploy failed: kill switch enabled for " + feature);
    }

    if (defer_native_until_restart) {
        std::string versionReasonDefer;
        if (!validateTargetVersion(*vmOpt, feature, target_slot, target_version, versionReasonDefer)) {
            const std::string failed = "failed";
            const std::string failedMode = "FAILED";
            const bool notRunning = false;
            (void)sm.updateOtaProgressForDeploy(car_id, &failed, &failed, &failedMode, &notRunning, err);
            (void)sm.updateOtaPhase(car_id, "CHECKSUM_FAILED", versionReasonDefer, err);
            std::cout << "[OTA] target=" << car_id << " mode=FAILED (defer-native validation)\n";
            return fail(versionReasonDefer);
        }

        const StateManager::FeatureState prevSnapshotDefer = *fs;
        const std::string rollback_id = generateRollbackIdFleet();
        const std::string rollback_ts = currentTimestamp();
        const std::filesystem::path absPathFs =
            std::filesystem::path(workDir_) / "deploy" / "rollback" / car_id / (feature + ".json");
        const std::string absPath = absPathFs.generic_string();
        const std::string stateRel = std::string("deploy/state/") + car_id + ".json";
        const std::string configRel = std::string("config/car/") + car_id + ".conf";
        const std::string backupRel =
            sync_config_requested ? (std::string("config/car/") + car_id + ".conf.bak") : std::string();

        {
            std::string phase1 = buildFleetRollbackMetadataPrettyJson(rollback_id, rollback_ts, car_id, feature,
                prevSnapshotDefer, target_slot, target_version, sync_config_requested, false, "", stateRel, configRel,
                backupRel);
            std::error_code mec;
            std::filesystem::create_directories(absPathFs.parent_path(), mec);
            if (mec) {
                return fail("rollback metadata save failed: cannot create directory " +
                    absPathFs.parent_path().generic_string() + ": " + mec.message());
            }
            std::string werr;
            if (!writeAtomicTextFile(absPath, phase1, werr)) {
                return fail("rollback metadata save failed: " + werr);
            }
        }

        rb.path = absPath;
        r.rollback_prev_feature = prevSnapshotDefer;
        r.rollback_prev_valid = true;
        r.rollback_meta_id = rollback_id;
        r.rollback_meta_timestamp = rollback_ts;
        r.rollback_meta_abs_path = absPath;

        std::cout << "[FLEET] defer-native: skipping OTA safety gate + demo OTA progression; "
                     "recording pending_active_* only (apply on next car start via apply-pending-native-car)\n";
        if (!sm.updateFeaturePending(car_id, feature, target_slot, target_version, err)) {
            return fail(err.empty() ? "deploy failed: unable to update pending feature fields" : err);
        }

        rb.commit();
        r.rollback_metadata_path = std::string("deploy/rollback/") + car_id + "/" + feature + ".json";
        r.rollback_metadata_saved = true;
        r.success = true;
        r.reason.clear();
        recordOutcome("pending", "deferred until apply-pending-native-car");
        std::cout << "[DEPLOY] target=" << car_id << " feature=" << feature << " slot=" << target_slot
                  << " version=" << target_version << " result=SUCCESS (pending until restart)\n";
        return r;
    }

    std::string safetyReason;
    if (!checkOtaSafety(*stateOpt, safetyReason)) {
        // Safety failure: must not change state.
        return fail(safetyReason);
    }

    std::string versionReason;
    if (!validateTargetVersion(*vmOpt, feature, target_slot, target_version, versionReason)) {
        // Internal failure after safety checks: reflect OTA failure.
        const std::string failed = "failed";
        const std::string failedMode = "FAILED";
        const bool notRunning = false;
        (void)sm.updateOtaProgressForDeploy(car_id, &failed, &failed, &failedMode, &notRunning, err);
        (void)sm.updateOtaPhase(car_id, "CHECKSUM_FAILED", versionReason, err);
        std::cout << "[OTA] target=" << car_id << " mode=FAILED download_status=failed install_status=failed target_system_running=false\n";
        return fail(versionReason);
    }

    ManifestResolveHints manifestHints{};
    {
        std::string slotErr;
        if (const auto slotInfo = vmOpt->getInfo(feature, target_slot[0], slotErr)) {
            manifestHints = manifestHintsFromVersionInfo(*slotInfo);
            if (!manifestHints.package_path.empty() || !manifestHints.package_id.empty()) {
                std::cout << "[FLEET] version_map slot hints"
                          << (manifestHints.package_id.empty() ? "" : (" package_id=" + manifestHints.package_id))
                          << (manifestHints.package_path.empty() ? "" : (" package_path=" + manifestHints.package_path))
                          << "\n";
            }
        }
    }

    // Rollback metadata MUST succeed before any on-disk state mutation for this deploy.
    const StateManager::FeatureState prevSnapshot = *fs;
    const std::string rollback_id = generateRollbackIdFleet();
    const std::string rollback_ts = currentTimestamp();
    const std::string relPath = std::string("deploy/rollback/") + car_id + "/" + feature + ".json";
    const std::filesystem::path absPathFs =
        std::filesystem::path(workDir_) / "deploy" / "rollback" / car_id / (feature + ".json");
    const std::string absPath = absPathFs.generic_string();
    const std::string stateRel = std::string("deploy/state/") + car_id + ".json";
    const std::string configRel = std::string("config/car/") + car_id + ".conf";
    const std::string backupRel =
        sync_config_requested ? (std::string("config/car/") + car_id + ".conf.bak") : std::string();

    {
        std::string phase1 = buildFleetRollbackMetadataPrettyJson(rollback_id, rollback_ts, car_id, feature,
            prevSnapshot, target_slot, target_version, sync_config_requested, false, "", stateRel, configRel,
            backupRel);
        std::error_code mec;
        std::filesystem::create_directories(absPathFs.parent_path(), mec);
        if (mec) {
            return fail("rollback metadata save failed: cannot create directory " +
                        absPathFs.parent_path().generic_string() + ": " + mec.message());
        }
        std::string werr;
        if (!writeAtomicTextFile(absPath, phase1, werr)) {
            return fail("rollback metadata save failed: " + werr);
        }
    }

    rb.path = absPath;
    r.rollback_prev_feature = prevSnapshot;
    r.rollback_prev_valid = true;
    r.rollback_meta_id = rollback_id;
    r.rollback_meta_timestamp = rollback_ts;
    r.rollback_meta_abs_path = absPath;

    // Fleet deploy intentionally updates ONLY `deploy/state/car_*.json` via StateManager.
    // It does NOT modify `config/car/car_*.conf` (that remains a separate release/config sync step).
    std::cout << "[FLEET] note: fleet deploy updates deploy/state only (config/car/*.conf is not modified)\n";

    // OTA state flow (no sleeps; just state transitions).
    // Keep legacy ota fields (download_status / install_status / update_mode) for compatibility,
    // but also write an explicit `ota_phase` to make phase separation visible in state JSON.
    //
    // ACK vs OTA phase:
    // - `ota.ota_phase`: deployment-manager tracked progress (this function writes it)
    // - `last_ack`: vehicle-reported acknowledgement (written via fleet-ack CLI; transport TBD)
    std::cout << "[FLEET] note: demo integrated OTA progression (download/verify/install are recorded as phases)\n";
    const std::string downloading = "downloading";
    const std::string notStarted = "not_started";
    const std::string downMode = "DOWNLOADING";
    const bool notRunning = false;
    if (!sm.updateOtaProgressForDeploy(car_id, &downloading, &notStarted, &downMode, &notRunning, err)) {
        return fail(err.empty() ? "deploy failed: unable to update OTA (DOWNLOADING)" : err);
    }
    (void)sm.updateOtaPhase(car_id, "DOWNLOAD_STARTED", "", err);
    std::cout << "[OTA] target=" << car_id << " mode=DOWNLOADING download_status=downloading\n";

    const std::string downloaded = "downloaded";
    const std::string readyToInstall = "READY_TO_INSTALL";
    if (!sm.updateOtaProgressForDeploy(car_id, &downloaded, nullptr, &readyToInstall, nullptr, err)) {
        const std::string failed = "failed";
        const std::string failedMode = "FAILED";
        (void)sm.updateOtaProgressForDeploy(car_id, &failed, &failed, &failedMode, &notRunning, err);
        (void)sm.updateOtaPhase(car_id, "DOWNLOAD_FAILED", "unable to update READY_TO_INSTALL", err);
        std::cout << "[OTA] target=" << car_id << " mode=FAILED download_status=failed install_status=failed\n";
        return fail(err.empty() ? "deploy failed: unable to update OTA (READY_TO_INSTALL)" : err);
    }
    (void)sm.updateOtaPhase(car_id, "DOWNLOAD_COMPLETED", "", err);
    (void)sm.updateOtaPhase(car_id, "VERIFYING", "", err);
    // Verification stage (metadata-level):
    // - For legacy/executable packages: validatePackageFile() does local file checks + a lightweight hash.
    // - For docker_image packages: verify presence of manifest/docker_image/checksum_file and optionally image_digest.
    //   This step does NOT contact a remote registry; digest verification against registry is a follow-up feature.
    auto verifyFail = [&](const std::string& reason) {
        const std::string failed = "failed";
        const std::string failedMode = "FAILED";
        (void)sm.updateOtaProgressForDeploy(car_id, &failed, &failed, &failedMode, &notRunning, err);
        (void)sm.updateOtaPhase(car_id, "CHECKSUM_FAILED", reason, err);
        return fail(reason);
    };
    auto verifyDockerMetadataIfDetected = [&]() -> bool {
        const auto resolvedOpt =
            resolvePackageManifest(packagesDir_, workDir_, feature, target_version, &manifestHints);
        if (!resolvedOpt) {
            return true;  // no manifest: legacy/native package path
        }

        ResolvedPackageManifest m = *resolvedOpt;
        const std::string manifestJson = readFileTextBestEffortLocal(m.abs_path);
        if (manifestJson.empty()) {
            return false;
        }
        parsePackageManifestFields(manifestJson, m);

        const bool explicitDocker = (m.artifact_type == "docker_image");
        const bool hasDockerImage = !m.docker_image.empty();
        if (!explicitDocker && !hasDockerImage) {
            return true;  // registry-style manifest without docker deployment metadata
        }
        if (!explicitDocker && m.artifact_type == "executable") {
            return true;
        }
        if (!explicitDocker && !m.artifact_type.empty() && m.artifact_type != "docker_image") {
            return false;
        }

        if (m.docker_image.empty()) {
            return false;
        }

        if (m.checksum_file.empty() && m.image_digest.empty()) {
            return false;
        }

        if (!m.checksum_file.empty()) {
            const auto baseDir = std::filesystem::path(m.abs_path).parent_path();
            const auto checksumAbs = (baseDir / m.checksum_file).generic_string();
            std::error_code ec;
            const bool checksumExists = std::filesystem::exists(checksumAbs, ec) && !ec;
            if (!checksumExists && m.image_digest.empty()) {
                return false;
            }
        }

        if (m.image_digest.empty() && !m.checksum_file.empty()) {
            std::cout << "[VERIFY] warning: image_digest missing (metadata-only); checksum_file present, continue\n";
        }
        return true;
    };

    if (!verifyDockerMetadataIfDetected()) {
        return verifyFail("docker metadata verification failed (missing manifest/docker_image/checksum/digest)");
    }

    (void)sm.updateOtaPhase(car_id, "VERIFIED", "", err);
    (void)sm.updateOtaPhase(car_id, "READY_TO_INSTALL", "", err);
    std::cout << "[OTA] target=" << car_id << " mode=READY_TO_INSTALL download_status=downloaded\n";

    const std::string installing = "installing";
    const std::string installingMode = "INSTALLING";
    const bool running = true;
    if (!sm.updateOtaProgressForDeploy(car_id, nullptr, &installing, &installingMode, &running, err)) {
        const std::string failed = "failed";
        const std::string failedMode = "FAILED";
        (void)sm.updateOtaProgressForDeploy(car_id, &failed, &failed, &failedMode, &notRunning, err);
        (void)sm.updateOtaPhase(car_id, "INSTALL_FAILED", "unable to update INSTALLING", err);
        std::cout << "[OTA] target=" << car_id << " mode=FAILED download_status=failed install_status=failed target_system_running=false\n";
        return fail(err.empty() ? "deploy failed: unable to update OTA (INSTALLING)" : err);
    }
    (void)sm.updateOtaPhase(car_id, "INSTALL_STARTED", "", err);
    std::cout << "[OTA] target=" << car_id << " mode=INSTALLING install_status=installing target_system_running=true\n";

    // Capture previous primary selection for rollback_stack recording (best-effort).
    // NOTE: This is metadata only; it does not change rollback behavior yet.
    std::string prev_slot;
    std::string prev_version;
    if (target_slot == "B") {
        r.staged_primary_slot =
            prevSnapshot.primary_slot.empty() ? prevSnapshot.active_slot : prevSnapshot.primary_slot;
        r.staged_primary_version = prevSnapshot.primary_version.empty() ? prevSnapshot.active_version
                                                                        : prevSnapshot.primary_version;
    }
    {
        auto stPrev = sm.loadState(car_id, err);
        if (stPrev) {
            const auto* fsPrev = StateManager::featureRef(*stPrev, feature);
            if (fsPrev) {
                prev_slot = fsPrev->active_slot;
                prev_version = fsPrev->active_version;
            }
        }
        err.clear();
    }

    // Update state: active_slot/active_version only. Keep flag/fallback/kill/shadow unchanged.
    if (!sm.updateFeatureSlot(car_id, feature, target_slot, target_version, err)) {
        const std::string failed = "failed";
        const std::string failedMode = "FAILED";
        (void)sm.updateOtaProgressForDeploy(car_id, &failed, &failed, &failedMode, &notRunning, err);
        (void)sm.updateOtaPhase(car_id, "INSTALL_FAILED", "unable to update feature slot", err);
        std::cout << "[OTA] target=" << car_id << " mode=FAILED download_status=failed install_status=failed target_system_running=false\n";
        return fail(err.empty() ? "deploy failed: unable to update feature slot" : err);
    }

    if (const auto resolvedOpt =
            resolvePackageManifest(packagesDir_, workDir_, feature, target_version, &manifestHints)) {
        ResolvedPackageManifest resolved = *resolvedOpt;
        const std::string manifestJson = readFileTextBestEffortLocal(resolved.abs_path);
        if (!manifestJson.empty()) {
            parsePackageManifestFields(manifestJson, resolved);
            const StateManager::FeaturePackageMetadata meta = featureMetadataFromResolved(resolved, manifestHints);
            const bool hasMeta = !meta.primary_image.empty() || !meta.artifact_type.empty() || !meta.image_digest.empty()
                || !meta.manifest_path.empty() || !meta.package_id.empty();
            if (hasMeta) {
                std::string metaErr;
                if (!sm.applyFeaturePackageMetadata(car_id, feature, meta, metaErr)) {
                    std::cerr << "[FLEET] warning: unable to apply package metadata to state: "
                              << (metaErr.empty() ? "unknown" : metaErr) << "\n";
                } else {
                    std::cout << "[FLEET] package metadata applied"
                              << (meta.package_id.empty() ? "" : (" package_id=" + meta.package_id))
                              << (meta.primary_image.empty() ? "" : (" primary_image=" + meta.primary_image))
                              << (meta.artifact_type.empty() ? "" : (" artifact_type=" + meta.artifact_type))
                              << (meta.manifest_path.empty() ? "" : (" manifest_path=" + meta.manifest_path))
                              << "\n";
                }
            }
        }
    }

    // Record the previous stable primary into rollback_stack (most-recent-first), capped to 3.
    if (!prev_version.empty() && prev_version != target_version) {
        StateManager::RollbackStackEntry e{};
        e.slot = prev_slot;
        e.version = prev_version;
        e.docker_image = "";  // unknown in this step (docker metadata is tracked elsewhere)
        e.previous_role = "primary";
        e.recorded_at = nowTimestampIso();
        e.reason = "promoted_to_primary";
        e.status = "stable";
        std::string rberr;
        (void)sm.pushRollbackStackEntry(car_id, feature, e, 3, rberr);
    }

    const std::string installed = "installed";
    const std::string completed = "COMPLETED";
    if (!sm.updateOtaProgressForDeploy(car_id, nullptr, &installed, &completed, &notRunning, err)) {
        std::cerr << "[OTA] target=" << car_id << " warning: unable to mark COMPLETED: "
                  << (err.empty() ? "unknown" : err) << "\n";
    } else {
        (void)sm.updateOtaPhase(car_id, "INSTALL_COMPLETED", "", err);
        std::cout << "[OTA] target=" << car_id << " mode=COMPLETED install_status=installed target_system_running=false\n";
    }

    rb.commit();
    r.rollback_metadata_path = relPath;
    r.rollback_metadata_saved = true;

    r.success = true;
    r.reason.clear();
    recordOutcome("success", "");
    std::cout << "[DEPLOY] target=" << car_id << " feature=" << feature << " slot=" << target_slot
              << " version=" << target_version << " result=SUCCESS\n";
    return r;
}

void DeploymentManager::finalizeFleetRollbackMetadata(DeployResult& r) {
    if (!r.success || !r.rollback_prev_valid || r.rollback_meta_abs_path.empty()) {
        return;
    }
    const std::string stateRel = std::string("deploy/state/") + r.car_id + ".json";
    const std::string configRel = std::string("config/car/") + r.car_id + ".conf";
    const std::string backupRel =
        r.config_sync_requested ? (std::string("config/car/") + r.car_id + ".conf.bak") : std::string();
    const std::string json = buildFleetRollbackMetadataPrettyJson(r.rollback_meta_id, r.rollback_meta_timestamp,
        r.car_id, r.feature, r.rollback_prev_feature, r.target_slot, r.target_version, r.config_sync_requested,
        r.config_synced, r.config_sync_error, stateRel, configRel, backupRel);
    std::string werr;
    if (!writeAtomicTextFile(r.rollback_meta_abs_path, json, werr)) {
        const std::string msg = "finalize rollback metadata: " + werr;
        r.rollback_metadata_error = r.rollback_metadata_error.empty() ? msg : (r.rollback_metadata_error + "; " + msg);
        std::cerr << "[FLEET] " << msg << "\n";
    }
}

void DeploymentManager::resyncCarConfigAfterDockerRuntime(DeployResult& r) {
    if (!r.config_sync_requested) {
        return;
    }
    std::string msg;
    if (syncCarConfigFromState(r.car_id, msg, true)) {
        r.config_synced = true;
        r.config_sync_error.clear();
        std::cout << "[DOCKER_RUNTIME] config_sync_after_runtime=ok car_id=" << r.car_id << " feature=" << r.feature
                  << "\n";
    } else {
        r.config_synced = false;
        r.config_sync_error = msg;
        std::cerr << "[DOCKER_RUNTIME] warning: config sync after runtime failed car_id=" << r.car_id << " feature="
                  << r.feature << ": " << (msg.empty() ? "unknown" : msg) << "\n";
    }
}

void DeploymentManager::applyPostDeployConfigSync(DeployResult& r, bool sync_requested) {
    r.config_sync_requested = sync_requested;
    r.state_updated = r.success;
    r.config_synced = false;
    r.config_sync_error.clear();
    if (r.success && sync_requested) {
        std::string msg;
        if (syncCarConfigFromState(r.car_id, msg, true)) {
            r.config_synced = true;
        } else {
            r.config_synced = false;
            r.config_sync_error = msg;
            std::cerr << "[FLEET] config sync failed after successful state deploy car_id=" << r.car_id << ": " << msg
                      << "\n";
        }
    }
    finalizeFleetRollbackMetadata(r);
}

void DeploymentManager::applyPostDeployDockerRuntime(DeployResult& r, bool defer_native_deploy,
    const FleetDockerRuntimeOptions& options) {
    r.docker_runtime_requested = options.requested;
    r.docker_runtime_mode = options.mode;
    const std::string pullPolicy = toLower(trim(options.pull_policy));
    r.docker_runtime_pull_policy = pullPolicy;
    r.docker_pull_requested = !pullPolicy.empty();
    if (!options.requested) {
        return;
    }
    if (!r.success) {
        return;
    }

    const std::string slotRole = fleetSlotRoleLabel(r.target_slot);
    const std::string mode = toLower(trim(options.mode.empty() ? std::string("candidate") : options.mode));

    auto persistDockerRuntime = [&](const std::string& result, const std::string& action, const std::string& reason,
                                    const std::string& container, const std::string& image) {
        r.docker_runtime_result = result;
        r.docker_runtime_action = action;
        r.docker_runtime_reason = reason;
        r.docker_runtime_container = container;
        r.docker_runtime_image = image;
        StateManager sm(workDir_);
        StateManager::FeatureDockerRuntimeRecord rec;
        rec.result = result;
        rec.mode = mode;
        rec.action = action;
        rec.time = currentTimestamp();
        rec.reason = reason;
        rec.container = container;
        rec.image = image;
        rec.pull_policy = pullPolicy;
        std::string perr;
        if (!sm.applyFeatureDockerRuntime(r.car_id, r.feature, rec, perr)) {
            std::cerr << "[DOCKER_RUNTIME] warning: unable to persist docker runtime fields to state: "
                      << (perr.empty() ? "unknown" : perr) << "\n";
        }
    };

    std::cout << "[DOCKER_RUNTIME] requested=true mode=" << mode;
    if (mode == "promote") {
        std::cout << " action=promote";
    } else if (mode == "candidate") {
        std::cout << " action=deploy-shadow";
    }
    std::cout << " car_id=" << r.car_id << " feature=" << r.feature << " version=" << r.target_version;
    if (r.docker_pull_requested) {
        std::cout << " pull_policy=" << pullPolicy;
    }

    if (defer_native_deploy) {
        r.docker_runtime_action = "skipped";
        persistDockerRuntime("skipped", "skipped",
            "deferred native deploy; docker runtime requires --no-defer-native", "", "");
        std::cout << "\n[DOCKER_RUNTIME] result=skipped reason=\"deferred native deploy\"\n";
        return;
    }

    if (mode == "promote") {
        StateManager sm(workDir_);
        std::string loadErr;
        const auto stateOpt = sm.loadState(r.car_id, loadErr);
        std::string dockerImage;
        if (stateOpt) {
            const auto* fs = StateManager::featureRef(*stateOpt, r.feature);
            if (fs) {
                dockerImage = fs->primary_image;
            }
        }
        if (dockerImage.empty()) {
            persistDockerRuntime("failed", "promote", "missing docker_image in manifest/state", "", "");
            std::cout << "\n[DOCKER_RUNTIME] result=failed reason=\"missing docker_image in manifest/state\"\n";
            return;
        }
        r.docker_runtime_image = dockerImage;

        const auto scriptOpt = findOtaVehicleScriptPath(workDir_);
        if (!scriptOpt) {
            persistDockerRuntime("failed", "promote", "ota_vehicle.sh not found under sdv-ota-manager/scripts", "", "");
            std::cout << "\n[DOCKER_RUNTIME] result=failed reason=\"ota_vehicle.sh not found\"\n";
            return;
        }

        const auto domainOpt = fleetFeatureToOtaVehicleDomain(r.feature);
        if (!domainOpt) {
            persistDockerRuntime("failed", "promote", "unsupported feature for docker runtime: " + r.feature, "", "");
            std::cout << "\n[DOCKER_RUNTIME] result=failed reason=\"unsupported feature\"\n";
            return;
        }

        const std::string candidateContainer = candidateShadowContainerForCarFeature(r.car_id, r.feature);
        const std::string primaryContainer = primaryContainerForCarFeature(r.car_id, r.feature);
        r.docker_runtime_action = "promote";
        std::cout << "\n";

        if (!dockerContainerNameExists(candidateContainer)) {
            persistDockerRuntime("failed", "promote", "candidate container not found", primaryContainer, dockerImage);
            std::cout << "[DOCKER_RUNTIME] result=failed reason=\"candidate container not found\"\n";
            std::cout << "[DOCKER_RUNTIME] hint=\"Run deploy-shadow first (e.g. --docker-runtime=candidate) and verify "
                      << candidateContainer << " is running\"\n";
            return;
        }

        std::cout << "[DOCKER_RUNTIME] action=promote domain=" << *domainOpt << " script=" << *scriptOpt << "\n";

        std::string cmd = std::string("bash ") + bashSingleQuoteLocal(*scriptOpt) + " promote --car "
            + bashSingleQuoteLocal(r.car_id) + " --domain " + bashSingleQuoteLocal(*domainOpt) + " --version "
            + bashSingleQuoteLocal(r.target_version) + " --image " + bashSingleQuoteLocal(dockerImage);
        if (r.docker_pull_requested) {
            cmd += " --pull " + bashSingleQuoteLocal(pullPolicy);
        }
        cmd += " 2>&1";

        const ShellCaptureResult shell = runShellCapture(cmd);
        if (shell.exit_code == 0) {
            persistDockerRuntime("success", "promote", "", primaryContainer, dockerImage);
            std::string alignErr;
            if (!sm.reconcileFeatureWithVehicleRuntime(r.car_id, r.feature, true, dockerImage, r.target_version, "",
                    r.staged_primary_version, alignErr)) {
                std::cerr << "[DOCKER_RUNTIME] warning: unable to reconcile deploy/state after promote: "
                          << (alignErr.empty() ? "unknown" : alignErr) << "\n";
            } else {
                std::cout << "[DOCKER_RUNTIME] state_reconcile=ok phase=promote car_id=" << r.car_id
                          << " feature=" << r.feature << "\n";
            }
            resyncCarConfigAfterDockerRuntime(r);
            std::cout << "[DOCKER_RUNTIME] result=success container=" << primaryContainer << "\n";
            if (!shell.output.empty()) {
                const std::string tail = summarizeShellOutput(shell.output, 240);
                if (!tail.empty()) {
                    std::cout << "[DOCKER_RUNTIME] output=" << tail << "\n";
                }
            }
            return;
        }

        std::string reason = summarizeShellOutput(shell.output);
        if (reason.empty()) {
            reason = "ota_vehicle.sh promote exited with code " + std::to_string(shell.exit_code);
        }
        if (reason.find("candidate container not found") == std::string::npos
            && (reason.find("Shadow") != std::string::npos || reason.find("shadow") != std::string::npos)
            && reason.find("없") != std::string::npos) {
            reason = "candidate container not found";
        }
        persistDockerRuntime("failed", "promote", reason, primaryContainer, dockerImage);
        std::cout << "[DOCKER_RUNTIME] result=failed reason=\"" << reason << "\"\n";
        emitDockerRuntimeFailureHints(reason, dockerImage);
        return;
    }

    if (r.target_slot != "B") {
        r.docker_runtime_action = "skipped";
        persistDockerRuntime("skipped", "skipped",
            "docker runtime for primary slot is not enabled in this step; use candidate slot with --docker-runtime",
            "", "");
        std::cout << "\n[DOCKER_RUNTIME] result=skipped reason=\"primary slot docker runtime not enabled\"\n";
        return;
    }

    StateManager sm(workDir_);
    std::string loadErr;
    const auto stateOpt = sm.loadState(r.car_id, loadErr);
    std::string dockerImage;
    if (stateOpt) {
        const auto* fs = StateManager::featureRef(*stateOpt, r.feature);
        if (fs) {
            dockerImage = fs->primary_image;
        }
    }
    if (dockerImage.empty()) {
        persistDockerRuntime("failed", "deploy-shadow", "missing docker_image in manifest/state", "", "");
        std::cout << " image=(missing)\n";
        std::cout << "[DOCKER_RUNTIME] result=failed reason=\"missing docker_image in manifest/state\"\n";
        std::cout << "[DOCKER_RUNTIME] hint=\"Ensure manifest docker_image is set and fleet-deploy package metadata step succeeded\"\n";
        return;
    }
    r.docker_runtime_image = dockerImage;
    std::cout << " image=" << dockerImage << "\n";

    const auto scriptOpt = findOtaVehicleScriptPath(workDir_);
    if (!scriptOpt) {
        persistDockerRuntime("failed", "deploy-shadow", "ota_vehicle.sh not found under sdv-ota-manager/scripts", "", "");
        std::cout << "[DOCKER_RUNTIME] result=failed reason=\"ota_vehicle.sh not found\"\n";
        std::cout << "[DOCKER_RUNTIME] hint=\"Expected sdv-ota-manager/scripts/ota_vehicle.sh relative to platform workDir\"\n";
        return;
    }

    const auto domainOpt = fleetFeatureToOtaVehicleDomain(r.feature);
    if (!domainOpt) {
        persistDockerRuntime("failed", "deploy-shadow", "unsupported feature for docker runtime: " + r.feature, "", "");
        std::cout << "[DOCKER_RUNTIME] result=failed reason=\"unsupported feature\"\n";
        return;
    }

    const std::string container = candidateShadowContainerForCarFeature(r.car_id, r.feature);
    r.docker_runtime_action = "deploy-shadow";
    std::cout << "\n";
    std::cout << "[DOCKER_RUNTIME] action=deploy-shadow domain=" << *domainOpt << " script=" << *scriptOpt << "\n";

    std::string cmd = std::string("bash ") + bashSingleQuoteLocal(*scriptOpt) + " deploy-shadow --car "
        + bashSingleQuoteLocal(r.car_id) + " --domain " + bashSingleQuoteLocal(*domainOpt) + " --version "
        + bashSingleQuoteLocal(r.target_version) + " --image " + bashSingleQuoteLocal(dockerImage);
    if (r.docker_pull_requested) {
        cmd += " --pull " + bashSingleQuoteLocal(pullPolicy);
    }
    cmd += " 2>&1";

    const ShellCaptureResult shell = runShellCapture(cmd);
    if (shell.exit_code == 0) {
        persistDockerRuntime("success", "deploy-shadow", "", container, dockerImage);
        std::string alignErr;
        if (!sm.reconcileFeatureWithVehicleRuntime(r.car_id, r.feature, false, dockerImage, r.target_version,
                r.staged_primary_slot, r.staged_primary_version, alignErr)) {
            std::cerr << "[DOCKER_RUNTIME] warning: unable to reconcile deploy/state after candidate deploy: "
                      << (alignErr.empty() ? "unknown" : alignErr) << "\n";
        } else {
            std::cout << "[DOCKER_RUNTIME] state_reconcile=ok phase=candidate car_id=" << r.car_id
                      << " feature=" << r.feature << "\n";
        }
        resyncCarConfigAfterDockerRuntime(r);
        std::cout << "[DOCKER_RUNTIME] result=success container=" << container << "\n";
        if (!shell.output.empty()) {
            const std::string tail = summarizeShellOutput(shell.output, 240);
            if (!tail.empty()) {
                std::cout << "[DOCKER_RUNTIME] output=" << tail << "\n";
            }
        }
        return;
    }

    std::string reason = summarizeShellOutput(shell.output);
    if (reason.empty()) {
        reason = "ota_vehicle.sh deploy-shadow exited with code " + std::to_string(shell.exit_code);
    }
    if (reason.find("Docker 데몬") != std::string::npos || reason.find("docker ps") != std::string::npos
        || reason.find("DEPLOY-ABORT") != std::string::npos && reason.find("이미지가 없습니다") != std::string::npos) {
        // keep message as-is
    } else if (reason.find("Docker") != std::string::npos && reason.find("데몬") != std::string::npos) {
        // already Korean daemon hint from script
    }
    persistDockerRuntime("failed", "deploy-shadow", reason, container, dockerImage);
    std::cout << "[DOCKER_RUNTIME] result=failed reason=\"" << reason << "\"\n";
    emitDockerRuntimeFailureHints(reason, dockerImage);
}

void DeploymentManager::printDeploySummary(const DeploySummary& summary) const {
    std::ostringstream target;
    if (summary.target_type == "group_region") {
        target << "group:" << summary.group << ",region:" << summary.region;
    } else if (summary.target_type == "group") {
        target << "group:" << summary.group;
    } else if (summary.target_type == "region") {
        target << "region:" << summary.region;
    } else {
        target << summary.target_type;
    }

    std::cout << "[SUMMARY] deploy_id=" << summary.deploy_id
              << " timestamp=" << summary.timestamp
              << " target=" << target.str()
              << " feature=" << summary.feature
              << " slot=" << summary.target_slot
              << " version=" << summary.target_version
              << " total=" << summary.total
              << " success=" << summary.success
              << " failed=" << summary.failed
              << "\n";

    if (summary.total == 0) {
        std::cout << "[SUMMARY] failed_reason=" << (summary.failed_reason.empty() ? "no target vehicles" : summary.failed_reason) << "\n";
        std::cout << "[SUMMARY] success_targets=none\n";
        std::cout << "[SUMMARY] failed_targets=none\n";
        std::cout << "[SUMMARY] sync_config_requested=" << (summary.sync_config_requested ? "true" : "false")
                  << " config_sync_partial=0\n";
        return;
    }

    std::string successList;
    std::string failedList;
    if (summary.docker_runtime_requested) {
        std::cout << "[SUMMARY] docker_runtime_requested=true docker_runtime_mode="
                  << (summary.docker_runtime_mode.empty() ? "candidate" : summary.docker_runtime_mode);
        if (summary.docker_pull_requested) {
            std::cout << " docker_pull_requested=true docker_pull_policy="
                      << (summary.docker_pull_policy.empty() ? "missing" : summary.docker_pull_policy);
        }
        std::cout << "\n";
    }

    for (const auto& r : summary.results) {
        if (r.success) {
            if (!successList.empty()) successList += ",";
            successList += r.car_id;
        } else {
            if (!failedList.empty()) failedList += ",";
            failedList += r.car_id;
            failedList += "(";
            failedList += r.reason.empty() ? "unknown" : r.reason;
            failedList += ")";
        }
    }

    std::cout << "[SUMMARY] success_targets=" << (successList.empty() ? "none" : successList) << "\n";
    std::cout << "[SUMMARY] failed_targets=" << (failedList.empty() ? "none" : failedList) << "\n";

    if (summary.reason_counts.empty()) {
        std::cout << "[SUMMARY] reason_counts=none\n";
        return;
    }
    std::string rc;
    for (const auto& kv : summary.reason_counts) {
        if (!rc.empty()) rc += ",";
        rc += kv.first;
        rc += ":";
        rc += std::to_string(kv.second);
    }
    std::cout << "[SUMMARY] reason_counts=" << (rc.empty() ? "none" : rc) << "\n";

    if (summary.total > 0) {
        int config_sync_partial = 0;
        for (const auto& r : summary.results) {
            if (r.success && r.config_sync_requested && !r.config_synced) {
                ++config_sync_partial;
            }
        }
        std::cout << "[SUMMARY] sync_config_requested=" << (summary.sync_config_requested ? "true" : "false")
                  << " config_sync_partial=" << config_sync_partial << "\n";
        for (const auto& r : summary.results) {
            std::cout << "[SUMMARY] " << r.car_id << ": state_updated=" << (r.state_updated ? "true" : "false");
            if (summary.sync_config_requested) {
                std::cout << " config_synced=" << (r.config_synced ? "true" : "false");
            }
            if (!r.success) {
                std::cout << " reason=" << (r.reason.empty() ? "unknown" : r.reason);
            } else if (summary.sync_config_requested && !r.config_sync_error.empty()) {
                std::cout << " config_sync_error=" << r.config_sync_error;
            }
            if (r.rollback_metadata_saved) {
                std::cout << " rollback_metadata_saved=true rollback_metadata_path=" << r.rollback_metadata_path;
            }
            if (!r.rollback_metadata_error.empty()) {
                std::cout << " rollback_metadata_error=" << r.rollback_metadata_error;
            }
            if (r.docker_runtime_requested) {
                std::cout << " docker_runtime_result=" << (r.docker_runtime_result.empty() ? "unknown" : r.docker_runtime_result);
                if (!r.docker_runtime_image.empty()) {
                    std::cout << " docker_runtime_image=" << r.docker_runtime_image;
                }
                if (r.docker_pull_requested) {
                    std::cout << " docker_pull_requested=true docker_pull_policy="
                              << (r.docker_runtime_pull_policy.empty() ? "missing" : r.docker_runtime_pull_policy);
                }
                if (!r.docker_runtime_reason.empty()) {
                    std::cout << " docker_runtime_reason=" << r.docker_runtime_reason;
                }
            }
            std::cout << "\n";
        }
    }
}

std::string DeploymentManager::jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

std::string DeploymentManager::currentTimestamp() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream os;
    // NOTE: Timezone offset formatting is intentionally omitted (allowed by requirements).
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return os.str();
}

std::string DeploymentManager::generateDeployId() {
    static int counter = 0;
    const int n = ++counter;

    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
    localtime_r(&t, &tm);

    std::ostringstream os;
    os << "deploy_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_"
       << std::setw(3) << std::setfill('0') << n;
    return os.str();
}

std::string DeploymentManager::deploySummaryToJson(const DeploySummary& summary) const {
    std::ostringstream os;

    std::vector<std::string> successTargets;
    struct FailedTarget { std::string car_id; std::string reason; };
    std::vector<FailedTarget> failedTargets;
    successTargets.reserve(summary.results.size());
    failedTargets.reserve(summary.results.size());

    for (const auto& r : summary.results) {
        if (r.success) {
            successTargets.push_back(r.car_id);
        } else {
            failedTargets.push_back(FailedTarget{r.car_id, r.reason});
        }
    }

    os << "{";
    os << "\"deploy_id\":\"" << jsonEscape(summary.deploy_id) << "\",";
    os << "\"timestamp\":\"" << jsonEscape(summary.timestamp) << "\",";
    os << "\"target_type\":\"" << jsonEscape(summary.target_type) << "\",";
    os << "\"group\":\"" << jsonEscape(summary.group) << "\",";
    os << "\"region\":\"" << jsonEscape(summary.region) << "\",";
    os << "\"feature\":\"" << jsonEscape(summary.feature) << "\",";
    os << "\"target_slot\":\"" << jsonEscape(summary.target_slot) << "\",";
    os << "\"target_version\":\"" << jsonEscape(summary.target_version) << "\",";
    os << "\"action\":\"fleet_deploy\",";
    os << "\"scope\":\"" << jsonEscape(summary.target_type) << "\",";
    os << "\"slot\":\"" << jsonEscape(summary.target_slot) << "\",";
    os << "\"version\":\"" << jsonEscape(summary.target_version) << "\",";
    os << "\"sync_config_requested\":" << (summary.sync_config_requested ? "true" : "false") << ",";
    os << "\"docker_runtime_requested\":" << (summary.docker_runtime_requested ? "true" : "false") << ",";
    if (summary.docker_runtime_requested) {
        os << "\"docker_runtime_mode\":\"" << jsonEscape(summary.docker_runtime_mode.empty() ? "candidate" : summary.docker_runtime_mode)
           << "\",";
    }
    if (summary.docker_pull_requested) {
        os << "\"docker_pull_requested\":true,";
        os << "\"docker_pull_policy\":\"" << jsonEscape(summary.docker_pull_policy.empty() ? "missing" : summary.docker_pull_policy)
           << "\",";
    }
    os << "\"total\":" << summary.total << ",";
    os << "\"success\":" << summary.success << ",";
    os << "\"failed\":" << summary.failed << ",";
    if (summary.total == 0) {
        os << "\"failed_reason\":\"" << jsonEscape(summary.failed_reason.empty() ? "no target vehicles" : summary.failed_reason) << "\",";
    }

    os << "\"success_targets\":[";
    for (std::size_t i = 0; i < successTargets.size(); ++i) {
        if (i) os << ",";
        os << "\"" << jsonEscape(successTargets[i]) << "\"";
    }
    os << "],";

    os << "\"failed_targets\":[";
    for (std::size_t i = 0; i < failedTargets.size(); ++i) {
        if (i) os << ",";
        os << "{\"car_id\":\"" << jsonEscape(failedTargets[i].car_id)
           << "\",\"reason\":\"" << jsonEscape(failedTargets[i].reason) << "\"}";
    }
    os << "],";

    os << "\"targets\":[";
    for (std::size_t i = 0; i < summary.results.size(); ++i) {
        if (i) os << ",";
        os << "\"" << jsonEscape(summary.results[i].car_id) << "\"";
    }
    os << "],";

    os << "\"results\":[";
    for (std::size_t i = 0; i < summary.results.size(); ++i) {
        if (i) os << ",";
        const auto& r = summary.results[i];
        const char* outcome = "failed";
        if (r.success) {
            if (!r.config_sync_requested || r.config_synced) {
                outcome = "success";
            } else {
                outcome = "partial_success";
            }
        }
        os << "{"
           << "\"car_id\":\"" << jsonEscape(r.car_id) << "\","
           << "\"feature\":\"" << jsonEscape(r.feature) << "\","
           << "\"target_slot\":\"" << jsonEscape(r.target_slot) << "\","
           << "\"target_version\":\"" << jsonEscape(r.target_version) << "\","
           << "\"success\":" << (r.success ? "true" : "false") << ","
           << "\"reason\":\"" << jsonEscape(r.reason) << "\","
           << "\"state_updated\":" << (r.state_updated ? "true" : "false") << ","
           << "\"config_sync_requested\":" << (r.config_sync_requested ? "true" : "false") << ","
           << "\"config_synced\":" << (r.config_synced ? "true" : "false") << ","
           << "\"config_sync_error\":\"" << jsonEscape(r.config_sync_error) << "\","
           << "\"result\":\"" << jsonEscape(outcome) << "\","
           << "\"state_file\":\"" << jsonEscape(std::string("deploy/state/") + r.car_id + ".json") << "\","
           << "\"config_file\":\"" << jsonEscape(std::string("config/car/") + r.car_id + ".conf") << "\","
           << "\"rollback_metadata_saved\":" << (r.rollback_metadata_saved ? "true" : "false") << ","
           << "\"rollback_metadata_path\":\"" << jsonEscape(r.rollback_metadata_path) << "\","
           << "\"rollback_metadata_error\":\"" << jsonEscape(r.rollback_metadata_error) << "\","
           << "\"docker_runtime_requested\":" << (r.docker_runtime_requested ? "true" : "false") << ","
           << "\"docker_runtime_mode\":\"" << jsonEscape(r.docker_runtime_mode.empty() ? "candidate" : r.docker_runtime_mode)
           << "\","
           << "\"docker_runtime_result\":\"" << jsonEscape(r.docker_runtime_result) << "\","
           << "\"docker_runtime_action\":\"" << jsonEscape(r.docker_runtime_action) << "\","
           << "\"docker_runtime_reason\":\"" << jsonEscape(r.docker_runtime_reason) << "\"";
        if (!r.docker_runtime_image.empty()) {
            os << ",\"docker_runtime_image\":\"" << jsonEscape(r.docker_runtime_image) << "\"";
        }
        if (r.docker_pull_requested) {
            os << ",\"docker_pull_requested\":true"
               << ",\"docker_pull_policy\":\"" << jsonEscape(r.docker_runtime_pull_policy.empty() ? "missing" : r.docker_runtime_pull_policy)
               << "\"";
        }
        os << "}";
    }
    os << "]";

    os << ",\"reason_counts\":{";
    {
        bool first = true;
        for (const auto& kv : summary.reason_counts) {
            if (!first) os << ",";
            first = false;
            os << "\"" << jsonEscape(kv.first) << "\":" << kv.second;
        }
    }
    os << "}";

    os << "}";
    return os.str();
}

void DeploymentManager::printDeploySummaryJson(const DeploySummary& summary) const {
    std::cout << "[SUMMARY_JSON] " << deploySummaryToJson(summary) << "\n";
}

DeploymentManager::DeploySummary DeploymentManager::deployToGroupWithSummary(const std::string& group,
    const std::string& feature,
    const std::string& target_slot,
    const std::string& target_version,
    bool sync_config_after_success,
    const FleetDockerRuntimeOptions* docker_runtime) {
    const FleetDockerRuntimeOptions dockerOpts = fleetDockerRuntimeOrDefault(docker_runtime);
    DeploySummary s;
    s.deploy_id = generateDeployId();
    s.timestamp = currentTimestamp();
    s.target_type = "group";
    s.group = group;
    s.feature = feature;
    s.target_slot = target_slot;
    s.target_version = target_version;
    s.sync_config_requested = sync_config_after_success;
    s.docker_runtime_requested = dockerOpts.requested;
    s.docker_runtime_mode = dockerOpts.mode;
    s.docker_pull_requested = !dockerOpts.pull_policy.empty();
    s.docker_pull_policy = dockerOpts.pull_policy;

    std::string err;
    const auto registryOpt = VehicleRegistry::loadFromFile(workDir_ + "/config/fleet/vehicles.json", err);
    if (!registryOpt) {
        s.failed_reason = err.empty() ? "load vehicles.json failed" : err;
        return s;
    }
    const auto targets = registryOpt->findByGroup(group);
    s.total = static_cast<int>(targets.size());
    if (targets.empty()) {
        s.failed_reason = "no target vehicles";
        s.reason_counts[s.failed_reason] += 1;
        return s;
    }

    for (const auto& v : targets) {
        auto r = deployToVehicleInternal(v.car_id, feature, target_slot, target_version, sync_config_after_success, false);
        applyPostDeployConfigSync(r, sync_config_after_success);
        applyPostDeployDockerRuntime(r, false, dockerOpts);
        if (r.success) ++s.success; else ++s.failed;
        if (!r.success) {
            const std::string key = r.reason.empty() ? "unknown" : r.reason;
            s.reason_counts[key] += 1;
        }
        s.results.push_back(std::move(r));
    }
    return s;
}

DeploymentManager::DeploySummary DeploymentManager::deployToRegionWithSummary(const std::string& region,
    const std::string& feature,
    const std::string& target_slot,
    const std::string& target_version,
    bool sync_config_after_success,
    const FleetDockerRuntimeOptions* docker_runtime) {
    const FleetDockerRuntimeOptions dockerOpts = fleetDockerRuntimeOrDefault(docker_runtime);
    DeploySummary s;
    s.deploy_id = generateDeployId();
    s.timestamp = currentTimestamp();
    s.target_type = "region";
    s.region = region;
    s.feature = feature;
    s.target_slot = target_slot;
    s.target_version = target_version;
    s.sync_config_requested = sync_config_after_success;
    s.docker_runtime_requested = dockerOpts.requested;
    s.docker_runtime_mode = dockerOpts.mode;
    s.docker_pull_requested = !dockerOpts.pull_policy.empty();
    s.docker_pull_policy = dockerOpts.pull_policy;

    std::string err;
    const auto registryOpt = VehicleRegistry::loadFromFile(workDir_ + "/config/fleet/vehicles.json", err);
    if (!registryOpt) {
        s.failed_reason = err.empty() ? "load vehicles.json failed" : err;
        return s;
    }
    const auto targets = registryOpt->findByRegion(region);
    s.total = static_cast<int>(targets.size());
    if (targets.empty()) {
        s.failed_reason = "no target vehicles";
        s.reason_counts[s.failed_reason] += 1;
        return s;
    }

    for (const auto& v : targets) {
        auto r = deployToVehicleInternal(v.car_id, feature, target_slot, target_version, sync_config_after_success, false);
        applyPostDeployConfigSync(r, sync_config_after_success);
        applyPostDeployDockerRuntime(r, false, dockerOpts);
        if (r.success) ++s.success; else ++s.failed;
        if (!r.success) {
            const std::string key = r.reason.empty() ? "unknown" : r.reason;
            s.reason_counts[key] += 1;
        }
        s.results.push_back(std::move(r));
    }
    return s;
}

DeploymentManager::DeploySummary DeploymentManager::deployToGroupAndRegionWithSummary(const std::string& group,
    const std::string& region,
    const std::string& feature,
    const std::string& target_slot,
    const std::string& target_version,
    bool sync_config_after_success,
    const FleetDockerRuntimeOptions* docker_runtime) {
    const FleetDockerRuntimeOptions dockerOpts = fleetDockerRuntimeOrDefault(docker_runtime);
    DeploySummary s;
    s.deploy_id = generateDeployId();
    s.timestamp = currentTimestamp();
    s.target_type = "group_region";
    s.group = group;
    s.region = region;
    s.feature = feature;
    s.target_slot = target_slot;
    s.target_version = target_version;
    s.sync_config_requested = sync_config_after_success;
    s.docker_runtime_requested = dockerOpts.requested;
    s.docker_runtime_mode = dockerOpts.mode;
    s.docker_pull_requested = !dockerOpts.pull_policy.empty();
    s.docker_pull_policy = dockerOpts.pull_policy;

    std::string err;
    const auto registryOpt = VehicleRegistry::loadFromFile(workDir_ + "/config/fleet/vehicles.json", err);
    if (!registryOpt) {
        s.failed_reason = err.empty() ? "load vehicles.json failed" : err;
        return s;
    }
    const auto targets = registryOpt->findByGroupAndRegion(group, region);
    s.total = static_cast<int>(targets.size());
    if (targets.empty()) {
        s.failed_reason = "no target vehicles";
        s.reason_counts[s.failed_reason] += 1;
        return s;
    }

    for (const auto& v : targets) {
        auto r = deployToVehicleInternal(v.car_id, feature, target_slot, target_version, sync_config_after_success, false);
        applyPostDeployConfigSync(r, sync_config_after_success);
        applyPostDeployDockerRuntime(r, false, dockerOpts);
        if (r.success) ++s.success; else ++s.failed;
        if (!r.success) {
            const std::string key = r.reason.empty() ? "unknown" : r.reason;
            s.reason_counts[key] += 1;
        }
        s.results.push_back(std::move(r));
    }
    return s;
}

DeploymentManager::DeploySummary DeploymentManager::deployToVehicleWithSummary(const std::string& car_id,
    const std::string& feature,
    const std::string& target_slot,
    const std::string& target_version,
    bool sync_config_after_success,
    bool defer_native_until_restart,
    const FleetDockerRuntimeOptions* docker_runtime) {
    const FleetDockerRuntimeOptions dockerOpts = fleetDockerRuntimeOrDefault(docker_runtime);
    DeploySummary s;
    s.deploy_id = generateDeployId();
    s.timestamp = currentTimestamp();
    s.target_type = "vehicle";
    s.feature = feature;
    s.target_slot = target_slot;
    s.target_version = target_version;
    s.sync_config_requested = sync_config_after_success;
    s.docker_runtime_requested = dockerOpts.requested;
    s.docker_runtime_mode = dockerOpts.mode;
    s.docker_pull_requested = !dockerOpts.pull_policy.empty();
    s.docker_pull_policy = dockerOpts.pull_policy;
    s.total = 1;

    auto r = deployToVehicleInternal(car_id, feature, target_slot, target_version, sync_config_after_success,
        defer_native_until_restart);
    applyPostDeployConfigSync(r, sync_config_after_success);
    applyPostDeployDockerRuntime(r, defer_native_until_restart, dockerOpts);
    s.results.push_back(r);
    if (r.success) {
        s.success = 1;
    } else {
        s.failed = 1;
        const std::string key = r.reason.empty() ? "unknown" : r.reason;
        s.reason_counts[key] += 1;
    }
    return s;
}

void DeploymentManager::printFleetDeploySummary(const DeploySummary& summary) const {
    printDeploySummary(summary);
    emitSummaryJsonWithFleetHistory(deploySummaryToJson(summary));
}

std::string DeploymentManager::fleetDeploySummaryToJson(const DeploySummary& summary) const {
    return deploySummaryToJson(summary);
}

std::string DeploymentManager::prepareFleetHistoryLine(std::string json) const {
    static const char kFleetDeployAction[] = "\"action\":\"fleet_deploy\"";
    static const char kFleetDeployActionOut[] = "\"action\":\"fleet-deploy\"";
    const std::size_t pos = json.find(kFleetDeployAction);
    if (pos != std::string::npos) {
        json.replace(pos, sizeof(kFleetDeployAction) - 1, std::string(kFleetDeployActionOut));
    }
    if (json.find("\"timestamp\"") == std::string::npos) {
        if (!json.empty() && json.front() == '{') {
            json = std::string("{\"timestamp\":\"") + jsonEscape(currentTimestamp()) + "\"," + json.substr(1);
        }
    }
    return json;
}

bool DeploymentManager::appendFleetHistoryLineRaw(const std::string& line, std::string& error) const {
    error.clear();
    if (line.find('\n') != std::string::npos || line.find('\r') != std::string::npos) {
        error = "refusing JSONL line containing newline";
        return false;
    }
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(workDir_) / "deploy" / "history";
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        error = std::string("mkdir deploy/history failed: ") + ec.message();
        return false;
    }
    const std::filesystem::path file = dir / "fleet_history.jsonl";
    std::ofstream out(file, std::ios::app | std::ios::binary);
    if (!out) {
        error = "failed to open deploy/history/fleet_history.jsonl for append";
        return false;
    }
    out << line << '\n';
    if (!out.good()) {
        error = "write deploy/history/fleet_history.jsonl failed";
        return false;
    }
    return true;
}

std::string DeploymentManager::appendHistoryMetaToSummaryJson(std::string json, bool history_saved,
    const std::string& history_error) {
    while (!json.empty() && std::isspace(static_cast<unsigned char>(json.back()))) {
        json.pop_back();
    }
    if (json.empty() || json.back() != '}') {
        return json;
    }
    json.pop_back();
    json += ",\"history_saved\":" + std::string(history_saved ? "true" : "false");
    json += ",\"history_path\":\"deploy/history/fleet_history.jsonl\"";
    json += ",\"history_error\":\"" + jsonEscape(history_error) + "\"}";
    return json;
}

void DeploymentManager::emitSummaryJsonWithFleetHistory(const std::string& summaryJson) const {
    if (summaryJson.empty()) {
        std::cerr << "[HISTORY][WARN] empty summary json; skipping fleet history append\n";
        std::cout << "[SUMMARY_JSON] "
                  << appendHistoryMetaToSummaryJson("{}", false, "empty summary json") << "\n";
        return;
    }
    const std::string line = prepareFleetHistoryLine(summaryJson);
    std::string histErr;
    const bool histOk = appendFleetHistoryLineRaw(line, histErr);
    std::cout << "[SUMMARY_JSON] " << appendHistoryMetaToSummaryJson(summaryJson, histOk, histErr) << "\n";
    if (histOk) {
        std::cout << "[HISTORY] appended deploy/history/fleet_history.jsonl\n";
    } else {
        std::cerr << "[HISTORY][WARN] failed to append fleet history: " << histErr << "\n";
    }
}

std::string DeploymentManager::buildSyncConfigCarSummaryJson(const std::string& car_id, bool success,
    const std::string& message) const {
    std::ostringstream os;
    os << "{";
    os << "\"action\":\"sync-config-car\",";
    os << "\"scope\":\"vehicle\",";
    os << "\"car_id\":\"" << jsonEscape(car_id) << "\",";
    os << "\"feature\":\"\",";
    os << "\"result\":\"" << jsonEscape(success ? std::string("success") : std::string("failed")) << "\",";
    os << "\"reason\":\"" << jsonEscape(message) << "\",";
    os << "\"state_file\":\"deploy/state/" << jsonEscape(car_id) << ".json\",";
    os << "\"config_file\":\"config/car/" << jsonEscape(car_id) << ".conf\"";
    os << "}";
    return os.str();
}

bool DeploymentManager::deployToGroup(const std::string& group, const std::string& feature,
    const std::string& target_slot, const std::string& target_version) {
    const auto summary = deployToGroupWithSummary(group, feature, target_slot, target_version);
    printDeploySummary(summary);
    printDeploySummaryJson(summary);
    return summary.failed == 0;
}

bool DeploymentManager::deployToRegion(const std::string& region, const std::string& feature,
    const std::string& target_slot, const std::string& target_version) {
    const auto summary = deployToRegionWithSummary(region, feature, target_slot, target_version);
    printDeploySummary(summary);
    printDeploySummaryJson(summary);
    return summary.failed == 0;
}

bool DeploymentManager::deployToGroupAndRegion(const std::string& group, const std::string& region,
    const std::string& feature, const std::string& target_slot, const std::string& target_version) {
    const auto summary = deployToGroupAndRegionWithSummary(group, region, feature, target_slot, target_version);
    printDeploySummary(summary);
    printDeploySummaryJson(summary);
    return summary.failed == 0;
}

bool DeploymentManager::selfTestFleetDeploy() {
    std::cout << "[SELFTEST] fleet deploy smoke test started\n";

    std::string err;
    const auto registryOpt = VehicleRegistry::loadFromFile(workDir_ + "/config/fleet/vehicles.json", err);
    if (!registryOpt) {
        std::cerr << "[SELFTEST] FAIL: " << (err.empty() ? "load vehicles.json failed" : err) << "\n";
        return false;
    }
    const auto targets = registryOpt->findByGroupAndRegion("canary", "KR");
    std::cout << "[SELFTEST] canary+KR targets=" << targets.size() << "\n";
    for (const auto& v : targets) {
        std::cout << "[SELFTEST] target=" << v.car_id << "\n";
    }

    if (targets.empty()) {
        std::cerr << "[SELFTEST] FAIL: no targets for canary+KR\n";
        return false;
    }

    // Validate expected demo fleet slice (canonical canary+KR targets).
    {
        bool hasCar1 = false;
        bool hasCar10 = false;
        for (const auto& v : targets) {
            if (v.car_id == "car_1") hasCar1 = true;
            if (v.car_id == "car_10") hasCar10 = true;
        }
        if (!hasCar1 || !hasCar10 || targets.size() != 2) {
            std::cerr << "[SELFTEST] FAIL: expected canary+KR targets exactly {car_1,car_10}, got count="
                      << targets.size() << "\n";
            return false;
        }
    }

    // Validate version_map.json mapping for A/B slots (read-only check).
    const auto vmOpt = VersionManager::loadFromFile(workDir_ + "/deploy/version_map.json", err);
    if (!vmOpt) {
        std::cerr << "[SELFTEST] FAIL: " << (err.empty() ? "load version_map.json failed" : err) << "\n";
        return false;
    }
    {
        std::string e;
        const auto adasA = vmOpt->getInfo("ADAS", 'A', e);
        const auto adasB = vmOpt->getInfo("ADAS", 'B', e);
        const auto navA = vmOpt->getInfo("Navigation", 'A', e);
        const auto navB = vmOpt->getInfo("Navigation", 'B', e);
        const auto enA = vmOpt->getInfo("Energy", 'A', e);
        const auto enB = vmOpt->getInfo("Energy", 'B', e);
        if (!adasA || !adasB || !navA || !navB || !enA || !enB) {
            std::cerr << "[SELFTEST] FAIL: version_map missing expected A/B slots\n";
            return false;
        }
        if (adasA->version != "v0.1" || adasB->version != "v0.2") {
            std::cerr << "[SELFTEST] FAIL: ADAS version_map mismatch\n";
            return false;
        }
        if (navA->version != "v1.0" || navB->version != "v1.1") {
            std::cerr << "[SELFTEST] FAIL: Navigation version_map mismatch\n";
            return false;
        }
        if (enA->version != "v0.5" || enB->version != "v0.6") {
            std::cerr << "[SELFTEST] FAIL: Energy version_map mismatch\n";
            return false;
        }
    }

    StateManager sm(workDir_);

    // Snapshot current states to restore later (avoid leaving test mutations).
    std::vector<StateManager::VehicleState> original;
    for (const auto& v : targets) {
        auto s = sm.loadState(v.car_id, err);
        if (!s) {
            std::cerr << "[SELFTEST] FAIL: " << (err.empty() ? "loadState failed" : err) << "\n";
            return false;
        }
        original.push_back(*s);
    }

    auto restore = [&]() {
        for (const auto& s : original) {
            std::string e;
            sm.saveState(s.car_id, s, e);
        }
    };

    // Validate StateManager can load car_1 and that OTA safety includes update_mode=READY.
    // NOTE: demo fleet JSON commonly ships with driver_approved=false; approval is validated in Scenario A/B,
    // so here we temporarily enable approval only to validate the non-approval parts of `checkOtaSafety`.
    {
        const auto s1 = sm.loadState("car_1", err);
        if (!s1) {
            std::cerr << "[SELFTEST] FAIL: " << (err.empty() ? "loadState(car_1) failed" : err) << "\n";
            restore();
            return false;
        }
        if (s1->ota.update_mode != "READY") {
            std::cerr << "[SELFTEST] FAIL: expected car_1 baseline update_mode=READY for safety checks\n";
            restore();
            return false;
        }
        const bool prevApproved = s1->ota.driver_approved;
        if (!sm.updateDriverApproval("car_1", true, err)) {
            std::cerr << "[SELFTEST] FAIL: unable to temporarily set driver_approved=true: "
                      << (err.empty() ? "unknown" : err) << "\n";
            restore();
            return false;
        }
        const auto s1Approved = sm.loadState("car_1", err);
        if (!s1Approved) {
            std::cerr << "[SELFTEST] FAIL: " << (err.empty() ? "reload car_1 after approval toggle failed" : err) << "\n";
            (void)sm.updateDriverApproval("car_1", prevApproved, err);
            restore();
            return false;
        }
        std::string r;
        if (!checkOtaSafety(*s1Approved, r)) {
            std::cerr << "[SELFTEST] FAIL: expected car_1 to pass OTA safety when approved + update_mode=READY: " << r << "\n";
            (void)sm.updateDriverApproval("car_1", prevApproved, err);
            restore();
            return false;
        }
        if (!sm.updateDriverApproval("car_1", prevApproved, err)) {
            std::cerr << "[SELFTEST] FAIL: unable to restore driver_approved after safety probe: "
                      << (err.empty() ? "unknown" : err) << "\n";
            restore();
            return false;
        }
        std::cout << "[SELFTEST] OK: StateManager load + OTA safety includes update_mode=READY\n";
    }

    // Scenario A: driver_approved=false -> safety failure -> ota should NOT change.
    const auto origCar1 = sm.loadState("car_1", err);
    if (!origCar1) { restore(); return false; }

    if (!sm.updateDriverApproval("car_1", false, err)) { restore(); return false; }
    {
        std::string msg;
        const bool ok = deployToVehicle("car_1", "ADAS", "B", "v0.2", msg);
        if (ok) {
            std::cerr << "[SELFTEST] FAIL: expected deploy failure when driver_approved=false\n";
            restore();
            return false;
        }
        const auto after = sm.loadState("car_1", err);
        if (!after) { restore(); return false; }
        const bool otaUnchanged =
            after->ota.download_status == origCar1->ota.download_status &&
            after->ota.install_status == origCar1->ota.install_status &&
            after->ota.update_mode == origCar1->ota.update_mode &&
            after->ota.target_system_running == origCar1->ota.target_system_running;
        if (!otaUnchanged) {
            std::cerr << "[SELFTEST] FAIL: ota changed on safety failure\n";
            restore();
            return false;
        }
        std::cout << "[SELFTEST] Scenario A OK (ota unchanged)\n";
    }

    // Scenario B: driver_approved=true -> OTA flow logs + feature update.
    if (!sm.updateDriverApproval("car_1", true, err)) { restore(); return false; }
    {
        std::string msg;
        const bool ok = deployToVehicle("car_1", "ADAS", "B", "v0.2", msg);
        if (!ok) {
            std::cerr << "[SELFTEST] FAIL: expected deploy success when driver_approved=true\n";
            restore();
            return false;
        }
        const auto after = sm.loadState("car_1", err);
        if (!after) { restore(); return false; }
        if (after->adas.active_slot != "B" || after->adas.active_version != "v0.2") {
            std::cerr << "[SELFTEST] FAIL: feature state not updated\n";
            restore();
            return false;
        }
        if (after->ota.install_status != "installed" || after->ota.update_mode != "COMPLETED") {
            std::cerr << "[SELFTEST] FAIL: ota completion state mismatch\n";
            restore();
            return false;
        }
        if (after->ota.target_system_running) {
            std::cerr << "[SELFTEST] FAIL: target_system_running should be false after completion\n";
            restore();
            return false;
        }
        std::cout << "[SELFTEST] Scenario B OK (COMPLETED)\n";
    }

    // Scenario B completes an OTA lifecycle (update_mode becomes COMPLETED). Later scenarios assume a fresh READY gate,
    // so restore the baseline vehicle JSON state for all canary+KR targets before continuing.
    {
        std::string e;
        for (const auto& s : original) {
            if (!sm.saveState(s.car_id, s, e)) {
                std::cerr << "[SELFTEST] FAIL: unable to restore baseline state for " << s.car_id << ": "
                          << (e.empty() ? "unknown" : e) << "\n";
                restore();
                return false;
            }
        }
        std::cout << "[SELFTEST] restored baseline fleet states after Scenario B\n";
    }

    // Scenario C: confirm canary+KR still includes car_10.
    bool hasCar10 = false;
    for (const auto& v : targets) {
        if (v.car_id == "car_10") hasCar10 = true;
    }
    if (!hasCar10) {
        std::cerr << "[SELFTEST] FAIL: canary+KR missing car_10\n";
        restore();
        return false;
    }
    std::cout << "[SELFTEST] Scenario C OK (car_10 included)\n";

    // Scenario D: deploy summary (car_1 approved, car_10 not approved) => 1 success / 1 failed.
    if (!sm.updateDriverApproval("car_1", true, err)) { restore(); return false; }
    if (!sm.updateDriverApproval("car_10", false, err)) { restore(); return false; }
    const auto sum = deployToGroupAndRegionWithSummary("canary", "KR", "ADAS", "B", "v0.2");
    printDeploySummary(sum);
    printDeploySummaryJson(sum);
    if (sum.deploy_id.empty() || sum.timestamp.empty()) {
        std::cerr << "[SELFTEST] FAIL: missing deploy_id/timestamp\n";
        restore();
        return false;
    }
    if (sum.total != 2 || sum.success != 1 || sum.failed != 1) {
        std::cerr << "[SELFTEST] FAIL: summary counts mismatch\n";
        restore();
        return false;
    }
    bool okCar1 = false;
    bool failCar10 = false;
    for (const auto& rr : sum.results) {
        if (rr.car_id == "car_1" && rr.success) okCar1 = true;
        if (rr.car_id == "car_10" && !rr.success && rr.reason.find("driver approval required") != std::string::npos) {
            failCar10 = true;
        }
    }
    if (!okCar1 || !failCar10) {
        std::cerr << "[SELFTEST] FAIL: summary target list mismatch\n";
        restore();
        return false;
    }
    auto it = sum.reason_counts.find("driver approval required");
    if (it == sum.reason_counts.end() || it->second != 1) {
        std::cerr << "[SELFTEST] FAIL: reason_counts mismatch\n";
        restore();
        return false;
    }
    std::cout << "[SELFTEST] Scenario D OK (summary)\n";

    // Scenario E: unknown group => total=0 and reason_counts contains "no target vehicles":1
    const auto sum0 = deployToGroupWithSummary("unknown", "ADAS", "B", "v0.2");
    printDeploySummary(sum0);
    printDeploySummaryJson(sum0);
    if (sum0.deploy_id.empty() || sum0.timestamp.empty()) {
        std::cerr << "[SELFTEST] FAIL: missing deploy_id/timestamp (zero targets)\n";
        restore();
        return false;
    }
    if (sum0.total != 0 || (!sum0.failed_reason.empty() && sum0.failed_reason != "no target vehicles")) {
        std::cerr << "[SELFTEST] FAIL: zero-target summary mismatch\n";
        restore();
        return false;
    }
    const auto it0 = sum0.reason_counts.find("no target vehicles");
    if (it0 == sum0.reason_counts.end() || it0->second != 1) {
        std::cerr << "[SELFTEST] FAIL: zero-target reason_counts mismatch\n";
        restore();
        return false;
    }
    std::cout << "[SELFTEST] Scenario E OK (zero targets)\n";

    restore();

    {
        std::error_code ec;
        for (const char* cid : {"car_1", "car_10"}) {
            const std::filesystem::path rb =
                std::filesystem::path(workDir_) / "deploy" / "rollback" / cid / "ADAS.json";
            std::filesystem::remove(rb, ec);
        }
    }

    std::cout << "[SELFTEST] SUCCESS\n";
    return true;
}

bool DeploymentManager::ensureDirs() {
    auto mkdirs = [](const std::string& path) -> bool {
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec) {
            std::cerr << "[DeploymentManager] Failed to create directory: " << path
                      << " (" << ec.message() << ")\n";
            return false;
        }
        return true;
    };

    if (!mkdirs(packagesDir_)) return false;
    if (!mkdirs(stagingDir_)) return false;
    if (!mkdirs(activeDir_)) return false;
    if (!mkdirs(backupDir_)) return false;
    if (!mkdirs(workDir_ + "/deploy/history")) return false;
    if (!mkdirs(metadataDir_)) return false;
    if (!mkdirs(stacksDir_)) return false;
    return true;
}

std::string DeploymentManager::nowTimestampForFilename() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream ts;
    ts << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return ts.str();
}

bool DeploymentManager::copyFileWithPerms(const std::string& src, const std::string& dst) {
    std::error_code ec;
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }
    std::error_code pec;
    const auto perms = std::filesystem::status(src, pec).permissions();
    if (!pec) {
        std::filesystem::permissions(dst, perms, std::filesystem::perm_options::replace, pec);
    }
    return true;
}

bool DeploymentManager::validatePackageFile(const std::string& path, ValidationInfo& out) {
    out = ValidationInfo{};
    out.result = "failed";

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        out.reason = "package missing";
        return false;
    }
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec) {
        out.reason = "unable to read file size";
        return false;
    }
    if (sz == 0) {
        out.reason = "package is empty";
        return false;
    }

    // Legacy / executable-artifact validation (prototype):
    // - This validates a local file (e.g. packaged executable copied into deploy/staging).
    // - It is NOT a Docker image digest verification.
    //
    // Docker-image based packaging (team direction):
    // - Preferred integrity pin is `image_digest` (sha256) from `docker image inspect` / registry.
    // - `checksum_file` is a *secondary* signal for artifacts in the image build context.
    //
    // We keep this lightweight FNV-1a 64-bit hash for demo portability (no external deps).
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        out.reason = "unable to open file";
        return false;
    }
    std::uint64_t h = 1469598103934665603ull;
    char buf[8192];
    while (in.good()) {
        in.read(buf, sizeof(buf));
        const std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            h ^= static_cast<unsigned char>(buf[i]);
            h *= 1099511628211ull;
        }
    }
    if (!in.eof() && in.fail()) {
        out.reason = "hash read failed";
        return false;
    }
    out.hash = hexU64(h);
    out.result = "success";
    out.reason = "basic checks passed";
    return true;
}

bool DeploymentManager::backupExistingActiveIfAny(const std::string& execBasename) {
    const std::string activePath = activeDir_ + "/" + execBasename;
    std::error_code ec;
    if (!std::filesystem::exists(activePath, ec)) {
        std::cout << "[DEPLOY] previous active backed up (none)\n";
        return true;
    }

    const std::string stamp = nowTimestampForFilename();
    const std::string backupPath = backupDir_ + "/" + execBasename + "." + stamp;

    std::filesystem::rename(activePath, backupPath, ec);
    if (!ec) {
        std::cout << "[DEPLOY] previous active backed up\n";
        return true;
    }

    // Cross-device or permission edge cases: fallback to copy+remove.
    if (!copyFileWithPerms(activePath, backupPath)) {
        std::cerr << "[DEPLOY] backup failed: " << ec.message() << "\n";
        return false;
    }
    std::filesystem::remove(activePath, ec);
    std::cout << "[DEPLOY] previous active backed up\n";
    return true;
}

bool DeploymentManager::writeDeployMetadataPlaceholder(const PackageInfo& info,
    const std::string& stagingPath,
    const std::string& activePath,
    const ValidationInfo& validation) const {
    // Lightweight placeholder for upcoming inventory/validation features.
    // One file per deploy action; format is intentionally simple for demo readability.
    const std::string stamp = nowTimestampForFilename();
    const std::string metaPath = metadataDir_ + "/deploy_" + info.execBasename + "_" + stamp + ".txt";

    std::ofstream out(metaPath, std::ios::trunc);
    if (!out) {
        std::cerr << "[DeploymentManager] Failed to write metadata: " << metaPath << "\n";
        return false;
    }
    out << "action=deploy\n";
    out << "feature=" << info.featureName << "\n";
    out << "version=" << info.version << "\n";
    out << "exec=" << info.execBasename << "\n";
    out << "mode=" << info.mode << "\n";
    out << "source=" << info.sourcePath << "\n";
    out << "staging=" << stagingPath << "\n";
    out << "active=" << activePath << "\n";
    out << "status=" << info.status << "\n";
    out << "deployedAt=" << info.deployedAt << "\n";
    out << "validation_result=" << validation.result << "\n";
    out << "validation_reason=" << validation.reason << "\n";
    if (!validation.hash.empty()) {
        out << "validation_hash_fnv1a64=" << validation.hash << "\n";
    }
    return true;
}

bool DeploymentManager::safeBasename(const std::string& name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        return false;
    }
    return true;
}

bool DeploymentManager::inferPackage(const std::string& execBasename, PackageInfo& out) {
    out.execBasename = execBasename;
    out.mode = "primary";
    if (execBasename == "AdasFeatureShadow") {
        out.mode = "shadow";
        out.featureName = "ADAS";
        out.version = "A";
        return true;
    }
    if (execBasename == "NavigationFeatureShadow") {
        out.mode = "shadow";
        out.featureName = "NAV";
        out.version = "A";
        return true;
    }
    if (execBasename == "EvEnergyFeatureShadow") {
        out.mode = "shadow";
        out.featureName = "ENERGY";
        out.version = "A";
        return true;
    }

    std::string core = execBasename;
    const std::string shadowSuf = "_Shadow";
    if (core.size() > shadowSuf.size() &&
        core.compare(core.size() - shadowSuf.size(), shadowSuf.size(), shadowSuf) == 0) {
        out.mode = "shadow";
        core.resize(core.size() - shadowSuf.size());
    }

    auto endsWith = [&](const std::string& suf) {
        return core.size() >= suf.size() &&
            core.compare(core.size() - suf.size(), suf.size(), suf) == 0;
    };

    if (core == "AdasFeature") {
        out.featureName = "ADAS";
        out.version = "A";
        return true;
    }
    if (core == "NavigationFeature") {
        out.featureName = "NAV";
        out.version = "A";
        return true;
    }
    if (core == "EvEnergyFeature") {
        out.featureName = "ENERGY";
        out.version = "A";
        return true;
    }

    if (core.rfind("AdasFeature_", 0) == 0) {
        out.featureName = "ADAS";
    } else if (core.rfind("NavigationFeature_", 0) == 0) {
        out.featureName = "NAV";
    } else if (core.rfind("EvEnergyFeature_", 0) == 0) {
        out.featureName = "ENERGY";
    } else {
        return false;
    }

    if (endsWith("_A")) {
        out.version = "A";
        return true;
    }
    if (endsWith("_B")) {
        out.version = "B";
        return true;
    }
    return false;
}

bool DeploymentManager::loadOrInitSelection(ConfigUpdater::Selection& s) {
    if (ConfigUpdater::loadSelection(selectionPath_, s)) {
        return true;
    }
    s = ConfigUpdater::Selection{};
    ConfigUpdater::saveSelection(selectionPath_, s);
    return true;
}

std::string DeploymentManager::stackFile(const std::string& domain) const {
    return stacksDir_ + "/" + domain + ".stack";
}

bool DeploymentManager::pushStack(const std::string& domain, const std::string& ab) {
    const std::string path = stackFile(domain);
    ConfigUpdater::Selection sel0{};
    loadOrInitSelection(sel0);
    std::string seed = "A";
    if (domain == "ADAS") {
        seed = sel0.adas;
    } else if (domain == "NAV") {
        seed = sel0.nav;
    } else {
        seed = sel0.energy;
    }

    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
    }
    if (lines.empty()) {
        lines.push_back(seed);
    }
    if (!lines.empty() && lines.back() == ab) {
        return true;
    }
    lines.push_back(ab);
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    for (const auto& l : lines) {
        out << l << '\n';
    }
    return true;
}

bool DeploymentManager::popStack(const std::string& domain, std::string& previousAb) {
    const std::string path = stackFile(domain);
    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
    }
    if (lines.size() < 2) {
        return false;
    }
    lines.pop_back();
    previousAb = lines.back();

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    for (const auto& l : lines) {
        out << l << '\n';
    }
    return true;
}

void DeploymentManager::appendManifest(const PackageInfo& info) {
    std::ofstream out(manifestPath_, std::ios::app);
    if (!out) {
        return;
    }
    out << info.featureName << '|' << info.version << '|' << info.execBasename << '|'
        << info.deployedPath << '|' << info.deployedAt << '|' << info.status << '|'
        << info.mode << '\n';
}

std::string DeploymentManager::inventoryFile(const std::string& domain) const {
    return metadataDir_ + "/inventory_" + domain + ".state";
}

bool DeploymentManager::loadInventoryRecord(const std::string& domain, InventoryRecord& out) const {
    out = InventoryRecord{};
    std::ifstream in(inventoryFile(domain));
    if (!in) {
        return true;
    }
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        if (key == "stable_version") {
            out.stableVersion = val;
        } else if (key == "candidate_version") {
            out.candidateVersion = val;
        } else if (key == "active_version") {
            out.activeVersion = val;
        } else if (key == "last_deployment_status") {
            out.lastDeploymentStatus = val;
        } else if (key == "last_deployed_at") {
            out.lastDeployedAt = val;
        } else if (key == "rollback_available") {
            out.rollbackAvailable = (val == "yes");
        }
    }
    return true;
}

bool DeploymentManager::saveInventoryRecord(const std::string& domain,
    const InventoryRecord& record) const {
    std::ofstream out(inventoryFile(domain), std::ios::trunc);
    if (!out) {
        std::cerr << "[DeploymentManager] Failed to write inventory: " << domain << "\n";
        return false;
    }
    out << "stable_version=" << record.stableVersion << "\n";
    out << "candidate_version=" << record.candidateVersion << "\n";
    out << "active_version=" << record.activeVersion << "\n";
    out << "last_deployment_status=" << record.lastDeploymentStatus << "\n";
    out << "last_deployed_at=" << record.lastDeployedAt << "\n";
    out << "rollback_available=" << (record.rollbackAvailable ? "yes" : "no") << "\n";
    return true;
}

bool DeploymentManager::hasRollbackHistory(const std::string& domain) const {
    // Prototype-level signal: selection stack depth only.
    // This does not guarantee binary backup integrity or runtime-safe rollback.
    std::ifstream in(stackFile(domain));
    std::string line;
    int count = 0;
    while (std::getline(in, line)) {
        if (!trim(line).empty()) {
            ++count;
        }
    }
    return count >= 2;
}

bool DeploymentManager::updateInventoryOnDeploy(const PackageInfo& info,
    const std::string& deployStatus) {
    InventoryRecord record{};
    loadInventoryRecord(info.featureName, record);
    record.candidateVersion = info.version;
    record.lastDeploymentStatus = deployStatus;
    if (deployStatus != "FAILED") {
        record.lastDeployedAt = info.deployedAt;
    }
    record.rollbackAvailable = hasRollbackHistory(info.featureName);
    return saveInventoryRecord(info.featureName, record);
}

bool DeploymentManager::updateInventoryOnSelect(const std::string& domain, const std::string& ab) {
    InventoryRecord record{};
    loadInventoryRecord(domain, record);
    record.activeVersion = ab;
    record.stableVersion = ab;
    record.lastDeployedAt = nowTimestampIso();
    record.rollbackAvailable = hasRollbackHistory(domain);
    return saveInventoryRecord(domain, record);
}

bool DeploymentManager::updateInventoryOnRollback(const std::string& domain, const std::string& ab) {
    InventoryRecord record{};
    loadInventoryRecord(domain, record);
    record.activeVersion = ab;
    record.stableVersion = ab;
    record.lastDeploymentStatus = "ROLLED_BACK";
    record.lastDeployedAt = nowTimestampIso();
    record.rollbackAvailable = hasRollbackHistory(domain);
    return saveInventoryRecord(domain, record);
}

bool DeploymentManager::updateInventoryStatusOnly(const std::string& domain,
    const std::string& status) {
    InventoryRecord record{};
    loadInventoryRecord(domain, record);
    record.lastDeploymentStatus = status;
    record.rollbackAvailable = hasRollbackHistory(domain);
    return saveInventoryRecord(domain, record);
}

std::string DeploymentManager::expectedPrimaryExec(const std::string& domain, const std::string& ab) {
    if (domain == "ADAS") {
        return "AdasFeature_" + ab;
    }
    if (domain == "NAV") {
        return "NavigationFeature_" + ab;
    }
    return "EvEnergyFeature_" + ab;
}

bool DeploymentManager::configContainsExpectedPrimary(const std::string& configPath,
    const std::string& domain,
    const std::string& ab) {
    const std::string expected = "feature " + expectedPrimaryExec(domain, ab);
    std::ifstream in(configPath);
    std::string line;
    while (std::getline(in, line)) {
        if (trim(line) == expected) {
            return true;
        }
    }
    return false;
}

bool DeploymentManager::runActivationHealthCheck(const std::string& domain,
    const std::string& ab,
    const std::string& platformConfigPath,
    HealthCheckInfo& out) const {
    out = HealthCheckInfo{};
    out.result = "failed";

    const std::string expectedExec = expectedPrimaryExec(domain, ab);
    const std::string activePath = activeDir_ + "/" + expectedExec;
    std::error_code ec;
    if (!std::filesystem::exists(activePath, ec)) {
        out.reason = "active binary missing";
        return false;
    }

    ConfigUpdater::Selection sel{};
    if (!ConfigUpdater::loadSelection(selectionPath_, sel)) {
        out.reason = "selection file missing";
        return false;
    }
    const std::string selectedAb =
        (domain == "ADAS") ? sel.adas : ((domain == "NAV") ? sel.nav : sel.energy);
    if (selectedAb != ab) {
        out.reason = "selection mismatch";
        return false;
    }

    if (!configContainsExpectedPrimary(platformConfigPath, domain, ab)) {
        out.reason = "config does not reference expected feature";
        return false;
    }

    InventoryRecord record{};
    loadInventoryRecord(domain, record);
    if (record.activeVersion != ab) {
        out.reason = "inventory active version mismatch";
        return false;
    }

    out.result = "success";
    out.reason = "basic activation checks passed";
    return true;
}

bool DeploymentManager::deployExecutable(const std::string& execBasename) {
    if (!ensureDirs()) {
        std::cerr << "[DeploymentManager] Cannot deploy: deploy directory init failed.\n";
        return false;
    }
    if (!safeBasename(execBasename)) {
        std::cerr << "[DeploymentManager] Invalid executable name.\n";
        return false;
    }
    PackageInfo info;
    if (!inferPackage(execBasename, info)) {
        std::cerr << "[DeploymentManager] Unrecognized feature binary: " << execBasename << "\n";
        return false;
    }

    const std::string src = packagesDir_ + "/" + execBasename;
    const std::string staging = stagingDir_ + "/" + execBasename;
    const std::string dst = activeDir_ + "/" + execBasename;
    info.sourcePath = src;
    info.deployedPath = dst;

    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) {
        std::cerr << "[DeploymentManager] Missing package: " << src << "\n";
        info.status = "failed";
        info.deployedAt = nowTimestampIso();
        updateInventoryOnDeploy(info, "FAILED");
        history_.append(info, "deploy");
        return false;
    }
    std::cout << "[DEPLOY] package found\n";

    if (!copyFileWithPerms(src, staging)) {
        std::cerr << "[DeploymentManager] Copy to staging failed.\n";
        info.status = "failed";
        info.deployedAt = nowTimestampIso();
        updateInventoryOnDeploy(info, "FAILED");
        history_.append(info, "deploy");
        return false;
    }
    // Staging artifacts are intentionally retained for upcoming validation/approval steps.
    std::cout << "[DEPLOY] copied to staging\n";

    std::cout << "[DEPLOY] validation started\n";
    ValidationInfo validation{};
    if (!validatePackageFile(staging, validation)) {
        std::cerr << "[DEPLOY] validation failed: " << validation.reason << "\n";
        std::cerr << "[DEPLOY] deployment aborted before activation\n";

        info.status = "failed";
        info.deployedAt = nowTimestampIso();
        if (!writeDeployMetadataPlaceholder(info, staging, dst, validation)) {
            std::cerr << "[DEPLOY] warning: metadata placeholder write failed\n";
        }
        updateInventoryOnDeploy(info, "FAILED");
        history_.append(info, "deploy");
        return false;
    }
    std::cout << "[DEPLOY] validation success\n";

    if (!backupExistingActiveIfAny(execBasename)) {
        info.status = "failed";
        info.deployedAt = nowTimestampIso();
        updateInventoryOnDeploy(info, "FAILED");
        history_.append(info, "deploy");
        return false;
    }

    if (!copyFileWithPerms(staging, dst)) {
        std::cerr << "[DeploymentManager] Promote to active failed.\n";
        info.status = "failed";
        info.deployedAt = nowTimestampIso();
        updateInventoryOnDeploy(info, "FAILED");
        history_.append(info, "deploy");
        return false;
    }
    std::cout << "[DEPLOY] promoted to active\n";

    info.status = "success";
    info.deployedAt = nowTimestampIso();

    // Best-effort metadata for validation/inventory (prototype). Do not fail deploy on this.
    if (!writeDeployMetadataPlaceholder(info, staging, dst, validation)) {
        std::cerr << "[DEPLOY] warning: metadata placeholder write failed\n";
    }
    updateInventoryOnDeploy(info, "STAGED");

    history_.append(info, "deploy");
    appendManifest(info);
    std::cout << "[DeploymentManager] Deployed " << execBasename << " → " << dst << "\n";
    return true;
}

bool DeploymentManager::selectVersion(const std::string& domainIn, const std::string& ab,
    const std::string& platformConfigPath) {
    const std::string domain = toUpperDomain(domainIn);
    if (ab != "A" && ab != "B") {
        std::cerr << "[DeploymentManager] Version must be A or B.\n";
        return false;
    }
    if (domain != "ADAS" && domain != "NAV" && domain != "ENERGY") {
        std::cerr << "[DeploymentManager] Domain must be ADAS, NAV, or ENERGY.\n";
        return false;
    }

    ConfigUpdater::Selection sel{};
    loadOrInitSelection(sel);
    if (domain == "ADAS") {
        sel.adas = ab;
    } else if (domain == "NAV") {
        sel.nav = ab;
    } else {
        sel.energy = ab;
    }

    if (!pushStack(domain, ab)) {
        return false;
    }
    if (!ConfigUpdater::saveSelection(selectionPath_, sel)) {
        std::cerr << "[DeploymentManager] Failed to write selection file.\n";
        return false;
    }
    if (!ConfigUpdater::applyToConfig(platformConfigPath, sel)) {
        std::cerr << "[DeploymentManager] Failed to update config: " << platformConfigPath << "\n";
        return false;
    }

    PackageInfo info;
    info.featureName = domain;
    info.version = ab;
    info.execBasename = {};
    info.sourcePath = selectionPath_;
    info.deployedPath = platformConfigPath;
    info.deployedAt = {};
    info.status = "active";
    info.mode = "selection";
    history_.append(info, "select");
    updateInventoryOnSelect(domain, ab);

    std::cout << "[RELEASE] activation requested\n";
    std::cout << "[RELEASE] config updated\n";
    std::cout << "[RELEASE] health check started\n";

    HealthCheckInfo health{};
    PackageInfo healthInfo;
    healthInfo.featureName = domain;
    healthInfo.version = ab;
    healthInfo.execBasename = expectedPrimaryExec(domain, ab);
    healthInfo.sourcePath = selectionPath_;
    healthInfo.deployedPath = platformConfigPath;
    healthInfo.deployedAt = nowTimestampIso();
    healthInfo.mode = "health_check";

    if (!runActivationHealthCheck(domain, ab, platformConfigPath, health)) {
        std::cerr << "[RELEASE] health check failed: " << health.reason << "\n";
        updateInventoryStatusOnly(domain, "HEALTH_CHECK_FAILED");
        healthInfo.status = "failed";
        history_.append(healthInfo, "health_check");

        std::cerr << "[RELEASE] automatic rollback started\n";
        // Reuse the existing stack-based rollback path: select() already pushed the new choice,
        // so rollbackDomain() restores the previous stable selection from the domain stack.
        if (rollbackDomain(domain, platformConfigPath)) {
            std::cout << "[RELEASE] rollback success\n";
        } else {
            std::cerr << "[RELEASE] rollback failed\n";
        }
        return false;
    }

    updateInventoryStatusOnly(domain, "ACTIVATED");
    std::cout << "[RELEASE] health check success\n";
    healthInfo.status = "success";
    history_.append(healthInfo, "health_check");
    std::cout << "[RELEASE] activation completed successfully\n";

    std::cout << "[DeploymentManager] Active " << domain << " = " << ab
              << " (config updated)\n";
    return true;
}

bool DeploymentManager::rollbackDomain(const std::string& domainIn,
    const std::string& platformConfigPath) {
    const std::string domain = toUpperDomain(domainIn);
    if (domain != "ADAS" && domain != "NAV" && domain != "ENERGY") {
        std::cerr << "[DeploymentManager] Domain must be ADAS, NAV, or ENERGY.\n";
        return false;
    }
    std::string prev;
    if (!popStack(domain, prev)) {
        std::cerr << "[DeploymentManager] No rollback history for " << domain << ".\n";
        return false;
    }

    ConfigUpdater::Selection sel{};
    loadOrInitSelection(sel);
    if (domain == "ADAS") {
        sel.adas = prev;
    } else if (domain == "NAV") {
        sel.nav = prev;
    } else {
        sel.energy = prev;
    }
    if (!ConfigUpdater::saveSelection(selectionPath_, sel)) {
        return false;
    }
    if (!ConfigUpdater::applyToConfig(platformConfigPath, sel)) {
        return false;
    }

    PackageInfo info;
    info.featureName = domain;
    info.version = prev;
    info.status = "rollback";
    info.mode = "selection";
    history_.append(info, "rollback");
    updateInventoryOnRollback(domain, prev);
    std::cout << "[RELEASE] Rolled back " << domain << " to " << prev << "\n";
    return true;
}

bool DeploymentManager::syncConfig(const std::string& platformConfigPath) {
    ConfigUpdater::Selection sel{};
    loadOrInitSelection(sel);
    return ConfigUpdater::applyToConfig(platformConfigPath, sel);
}

namespace {

struct FleetRollbackPrevFields {
    std::string previous_slot;
    std::string previous_version;
    std::string previous_flag;
    bool previous_shadow_enabled{false};
    std::string previous_shadow_slot;
    std::string previous_shadow_version;
    bool previous_kill_switch{false};
    std::string previous_kill_reason;
    std::string previous_fallback_slot;
    std::string previous_fallback_version;
    std::string previous_realtime_route_flag;
    std::string previous_charging_recommend_flag;
    std::string previous_advanced_range_flag;
};

bool extractJsonStringKey(const std::string& json, const std::string& key, std::string& out) {
    const auto v = parseJsonStringFieldLooseLocal(json, key);
    if (!v.has_value()) {
        return false;
    }
    out = *v;
    return true;
}

bool extractJsonBoolKey(const std::string& json, const std::string& key, bool& out) {
    // Best-effort boolean field extractor. Expected pattern: "key": true|false
    const std::string needle = "\"" + key + "\"";
    const std::size_t kpos = json.find(needle);
    if (kpos == std::string::npos) {
        return false;
    }
    std::size_t pos = json.find(':', kpos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        pos++;
    }
    if (json.compare(pos, 4, "true") == 0) {
        out = true;
        return true;
    }
    if (json.compare(pos, 5, "false") == 0) {
        out = false;
        return true;
    }
    return false;
}

bool parseFleetRollbackMetadataFromJson(const std::string& json, FleetRollbackPrevFields& m, std::string& err) {
    err.clear();
    // Required minimal keys (legacy rollback files may omit newer fields).
    if (!extractJsonStringKey(json, "previous_slot", m.previous_slot) ||
        !extractJsonStringKey(json, "previous_version", m.previous_version)) {
        err = "rollback metadata parse failed: missing previous_slot/previous_version";
        return false;
    }

    // Best-effort optional keys.
    (void)extractJsonStringKey(json, "previous_flag", m.previous_flag);
    (void)extractJsonStringKey(json, "previous_shadow_slot", m.previous_shadow_slot);
    (void)extractJsonStringKey(json, "previous_shadow_version", m.previous_shadow_version);
    (void)extractJsonStringKey(json, "previous_kill_reason", m.previous_kill_reason);
    (void)extractJsonStringKey(json, "previous_fallback_slot", m.previous_fallback_slot);
    (void)extractJsonStringKey(json, "previous_fallback_version", m.previous_fallback_version);
    (void)extractJsonBoolKey(json, "previous_shadow_enabled", m.previous_shadow_enabled);
    (void)extractJsonBoolKey(json, "previous_kill_switch", m.previous_kill_switch);
    (void)extractJsonStringKey(json, "previous_realtime_route_flag", m.previous_realtime_route_flag);
    (void)extractJsonStringKey(json, "previous_charging_recommend_flag", m.previous_charging_recommend_flag);
    (void)extractJsonStringKey(json, "previous_advanced_range_flag", m.previous_advanced_range_flag);
    if (m.previous_slot.empty() || m.previous_version.empty()) {
        err = "rollback metadata parse failed: empty previous_slot or previous_version";
        return false;
    }
    return true;
}

bool applyFleetRollbackPrevToFeature(StateManager::FeatureState& fs,
    const FleetRollbackPrevFields& m,
    const std::string& feature,
    std::string& err) {
    err.clear();
    if (m.previous_slot != "A" && m.previous_slot != "B") {
        err = "invalid previous_slot in rollback metadata: " + m.previous_slot;
        return false;
    }
    if (m.previous_flag != "ON" && m.previous_flag != "OFF") {
        err = "invalid previous_flag in rollback metadata: " + m.previous_flag;
        return false;
    }
    if (!m.previous_shadow_slot.empty() && m.previous_shadow_slot != "A" && m.previous_shadow_slot != "B") {
        err = "invalid previous_shadow_slot in rollback metadata: " + m.previous_shadow_slot;
        return false;
    }
    if (m.previous_fallback_slot != "A" && m.previous_fallback_slot != "B") {
        err = "invalid previous_fallback_slot in rollback metadata: " + m.previous_fallback_slot;
        return false;
    }

    fs.active_slot = m.previous_slot;
    fs.active_version = m.previous_version;
    fs.primary_slot = m.previous_slot;
    fs.primary_version = m.previous_version;
    {
        const std::string vNoV = stripLeadingVTokenLocal(m.previous_version);
        if (!vNoV.empty()) {
            fs.primary_image = dockerRepoForFleetFeatureLocal(feature) + ":v" + vNoV;
        }
    }
    fs.flag = m.previous_flag;
    fs.shadow_enabled = m.previous_shadow_enabled;
    fs.shadow_slot = m.previous_shadow_slot;
    fs.shadow_version = m.previous_shadow_version;
    fs.kill_switch = m.previous_kill_switch;
    fs.kill_reason = m.previous_kill_reason;
    fs.fallback_slot = m.previous_fallback_slot;
    fs.fallback_version = m.previous_fallback_version;

    if (feature == "Navigation") {
        fs.realtime_route_flag = m.previous_realtime_route_flag;
        fs.charging_recommend_flag = m.previous_charging_recommend_flag;
        fs.advanced_range_flag.clear();
    } else if (feature == "Energy") {
        fs.advanced_range_flag = m.previous_advanced_range_flag;
        fs.charging_recommend_flag = m.previous_charging_recommend_flag;
        fs.realtime_route_flag.clear();
    } else {
        fs.realtime_route_flag.clear();
        fs.charging_recommend_flag.clear();
        fs.advanced_range_flag.clear();
    }
    return true;
}

bool isSyncConfigCarId(const std::string& car_id) {
    if (car_id.rfind("car_", 0) != 0) {
        return false;
    }
    const std::string suffix = car_id.substr(4);
    if (suffix == "10") {
        return true;
    }
    return suffix.size() == 1 && suffix[0] >= '1' && suffix[0] <= '9';
}

}  // namespace

bool DeploymentManager::syncCarConfigFromState(const std::string& car_id, std::string& outMessage, bool quietLog) {
    outMessage.clear();
    if (!isSyncConfigCarId(car_id)) {
        outMessage = "sync-config-car: invalid car_id (allowed: car_1 .. car_10): " + car_id;
        if (!quietLog) {
            std::cerr << "[SYNC-CAR] " << outMessage << "\n";
        }
        return false;
    }

    std::string err;
    StateManager sm(workDir_);
    const auto stateOpt = sm.loadState(car_id, err);
    if (!stateOpt) {
        outMessage = err.empty() ? ("sync-config-car: failed to load deploy/state/" + car_id + ".json") : err;
        if (!quietLog) {
            std::cerr << "[SYNC-CAR] " << outMessage << "\n";
        }
        return false;
    }

    const std::string configPath = workDir_ + "/config/car/" + car_id + ".conf";
    if (!ConfigUpdater::applyStateToCarConfig(configPath, *stateOpt, err)) {
        outMessage = err.empty() ? ("sync-config-car: failed to update " + configPath) : err;
        if (!quietLog) {
            std::cerr << "[SYNC-CAR] " << outMessage << "\n";
        }
        return false;
    }

    outMessage = "sync-config-car: updated " + configPath + " from deploy/state/" + car_id + ".json";
    if (!quietLog) {
        std::cout << "[SYNC-CAR] " << outMessage << "\n";
    }
    return true;
}

bool DeploymentManager::fleetRollbackVehicle(const std::string& car_id,
    const std::string& feature,
    bool sync_config,
    FleetRollbackSummary& summary,
    std::string& errorMessage,
    const std::string& target_version) {
    errorMessage.clear();
    summary = FleetRollbackSummary{};
    summary.car_id = car_id;
    summary.feature = feature;
    summary.sync_config_requested = sync_config;
    summary.metadata_path = "deploy/rollback/" + car_id + "/" + feature + ".json";
    summary.state_file = "deploy/state/" + car_id + ".json";
    summary.config_file = "config/car/" + car_id + ".conf";

    if (!isValidFleetFeature(feature)) {
        summary.result = "failed";
        summary.reason = "invalid feature (expected ADAS|Navigation|Energy): " + feature;
        errorMessage = summary.reason;
        return false;
    }
    if (!isSyncConfigCarId(car_id)) {
        summary.result = "failed";
        summary.reason = "invalid car_id (allowed: car_1 .. car_10): " + car_id;
        errorMessage = summary.reason;
        return false;
    }

    StateManager sm(workDir_);
    std::string err;
    auto stOpt = sm.loadState(car_id, err);
    if (!stOpt) {
        summary.result = "failed";
        summary.reason = err.empty() ? ("failed to load " + summary.state_file) : err;
        errorMessage = summary.reason;
        return false;
    }

    StateManager::FeatureState* fs = StateManager::featureRef(*stOpt, feature);
    if (!fs) {
        summary.result = "failed";
        summary.reason = "internal: unknown feature: " + feature;
        errorMessage = summary.reason;
        return false;
    }

    // Distinguish:
    // - Shadow validation failure: primary was never promoted; should go to blocked/quarantine (future wiring).
    // - Primary runtime failure after promote: this is a real rollback; we block the current primary and restore an older stable.
    //
    // In this step we keep the existing rollback behavior (rollback metadata file) as default for compatibility,
    // and add an optional selection path via `--target-version` that reads from feature.rollback_stack.

    // Record blocked current (best-effort; rollback should still proceed if this fails).
    {
        StateManager::BlockedVersionEntry b{};
        b.slot = fs->active_slot;
        b.version = fs->active_version;
        b.docker_image = "";
        b.blocked_at = nowTimestampIso();
        b.reason = "rollback invoked";
        b.source = "primary_runtime_failed";
        std::string berr;
        (void)sm.addBlockedVersion(car_id, feature, b, berr);
        // Reload state after potential modification so subsequent operations use current data.
        stOpt = sm.loadState(car_id, err);
        if (!stOpt) {
            summary.result = "failed";
            summary.reason = err.empty() ? ("failed to load " + summary.state_file) : err;
            errorMessage = summary.reason;
            return false;
        }
        fs = StateManager::featureRef(*stOpt, feature);
        if (!fs) {
            summary.result = "failed";
            summary.reason = "internal: unknown feature: " + feature;
            errorMessage = summary.reason;
            return false;
        }
    }

    bool restoredViaStack = false;
    if (!target_version.empty()) {
        // Choose a specific rollback target from rollback_stack.
        const auto it = std::find_if(fs->rollback_stack.begin(), fs->rollback_stack.end(),
            [&](const StateManager::RollbackStackEntry& e) { return e.version == target_version; });
        if (it == fs->rollback_stack.end()) {
            summary.result = "failed";
            summary.reason = "target version not found in rollback_stack: " + target_version;
            errorMessage = summary.reason;
            return false;
        }
        fs->active_slot = it->slot.empty() ? fs->active_slot : it->slot;
        fs->active_version = it->version;
        // Align Primary metadata to the restored active selection for consistency.
        fs->primary_slot = fs->active_slot;
        fs->primary_version = fs->active_version;
        if (!it->docker_image.empty()) {
            fs->primary_image = it->docker_image;
        }
        // Keep flag/kill/shadow/fallback/pending as-is in this step (metadata-only rollback selection).
        restoredViaStack = true;
    } else {
        // Default: preserve legacy behavior using deploy/rollback/<car>/<Feature>.json
        const std::filesystem::path metaAbs = std::filesystem::path(workDir_) / "deploy" / "rollback" / car_id / (feature + ".json");
        std::error_code fsec;
        if (!std::filesystem::exists(metaAbs, fsec)) {
            summary.result = "failed";
            summary.reason = "rollback metadata not found: " + summary.metadata_path;
            errorMessage = summary.reason;
            return false;
        }

        std::ifstream in(metaAbs);
        if (!in) {
            summary.result = "failed";
            summary.reason = "rollback metadata not found: " + summary.metadata_path;
            errorMessage = summary.reason;
            return false;
        }
        std::ostringstream buf;
        buf << in.rdbuf();
        const std::string content = buf.str();

        FleetRollbackPrevFields meta;
        if (!parseFleetRollbackMetadataFromJson(content, meta, errorMessage)) {
            summary.result = "failed";
            summary.reason = errorMessage.empty() ? "rollback metadata parse failed" : errorMessage;
            errorMessage = summary.reason;
            return false;
        }

        if (!applyFleetRollbackPrevToFeature(*fs, meta, feature, err)) {
            summary.result = "failed";
            summary.reason = err;
            errorMessage = summary.reason;
            return false;
        }
    }

    if (!sm.saveState(car_id, *stOpt, err)) {
        summary.result = "failed";
        summary.reason = err.empty() ? "saveState failed" : err;
        errorMessage = summary.reason;
        return false;
    }

    summary.restored_slot = fs->active_slot;
    summary.restored_version = fs->active_version;
    if (restoredViaStack) {
        summary.reason = "restored from rollback_stack target_version=" + target_version;
    }

    if (sync_config) {
        std::string msg;
        if (syncCarConfigFromState(car_id, msg, true)) {
            summary.config_synced = true;
            summary.result = "success";
            summary.reason.clear();
            return true;
        }
        summary.config_synced = false;
        summary.config_sync_error = msg;
        summary.result = "partial_success";
        summary.reason = msg.empty() ? "config sync failed after state rollback" : msg;
        return false;
    }

    summary.config_synced = false;
    summary.result = "success";
    summary.reason.clear();
    return true;
}

void DeploymentManager::printFleetRollbackSummary(const FleetRollbackSummary& s) const {
    std::cout << "[SUMMARY] action=" << s.action << " car_id=" << s.car_id << " feature=" << s.feature
              << " restored_slot=" << s.restored_slot << " restored_version=" << s.restored_version
              << " sync_config_requested=" << (s.sync_config_requested ? "true" : "false")
              << " config_synced=" << (s.config_synced ? "true" : "false") << " result=" << s.result
              << " reason=" << s.reason;
    if (!s.config_sync_error.empty()) {
        std::cout << " config_sync_error=" << s.config_sync_error;
    }
    std::cout << "\n";
}

std::string DeploymentManager::fleetRollbackSummaryToJson(const FleetRollbackSummary& s) const {
    std::ostringstream os;
    os << "{";
    os << "\"action\":\"" << jsonEscape(s.action) << "\",";
    os << "\"scope\":\"" << jsonEscape(s.scope) << "\",";
    os << "\"car_id\":\"" << jsonEscape(s.car_id) << "\",";
    os << "\"feature\":\"" << jsonEscape(s.feature) << "\",";
    os << "\"restored_slot\":\"" << jsonEscape(s.restored_slot) << "\",";
    os << "\"restored_version\":\"" << jsonEscape(s.restored_version) << "\",";
    os << "\"sync_config_requested\":" << (s.sync_config_requested ? "true" : "false") << ",";
    os << "\"config_synced\":" << (s.config_synced ? "true" : "false") << ",";
    os << "\"config_sync_error\":\"" << jsonEscape(s.config_sync_error) << "\",";
    os << "\"result\":\"" << jsonEscape(s.result) << "\",";
    os << "\"reason\":\"" << jsonEscape(s.reason) << "\",";
    os << "\"metadata_path\":\"" << jsonEscape(s.metadata_path) << "\",";
    os << "\"state_file\":\"" << jsonEscape(s.state_file) << "\",";
    os << "\"config_file\":\"" << jsonEscape(s.config_file) << "\"";
    os << "}";
    return os.str();
}

bool DeploymentManager::fleetFlagVehicle(const std::string& car_id,
    const std::string& feature,
    const std::string& new_flag,
    bool sync_config,
    FleetFlagSummary& summary,
    std::string& errorMessage) {
    errorMessage.clear();
    summary = FleetFlagSummary{};
    summary.car_id = car_id;
    summary.feature = feature;
    summary.new_flag = new_flag;
    summary.sync_config_requested = sync_config;
    summary.note =
        "flag state is stored in deploy/state; config sync only aligns active feature block";

    if (!isSyncConfigCarId(car_id)) {
        summary.result = "failed";
        summary.reason = "invalid car_id (allowed: car_1 .. car_10): " + car_id;
        errorMessage = summary.reason;
        return false;
    }
    summary.state_file = "deploy/state/" + car_id + ".json";
    summary.config_file = "config/car/" + car_id + ".conf";

    if (!isValidFleetFeature(feature)) {
        summary.result = "failed";
        summary.reason = "invalid feature (expected ADAS|Navigation|Energy): " + feature;
        errorMessage = summary.reason;
        return false;
    }

    if (new_flag != "ON" && new_flag != "OFF") {
        summary.result = "failed";
        summary.reason = "invalid flag (expected ON|OFF): " + new_flag;
        errorMessage = summary.reason;
        return false;
    }

    StateManager sm(workDir_);
    std::string err;
    auto stOpt = sm.loadState(car_id, err);
    if (!stOpt) {
        summary.result = "failed";
        summary.reason = err.empty() ? ("failed to load " + summary.state_file) : err;
        errorMessage = summary.reason;
        return false;
    }

    const StateManager::FeatureState* fsBefore = StateManager::featureRef(*stOpt, feature);
    if (!fsBefore) {
        summary.result = "failed";
        summary.reason = "internal: unknown feature: " + feature;
        errorMessage = summary.reason;
        return false;
    }

    summary.previous_flag = fsBefore->flag;
    summary.active_slot = fsBefore->active_slot;
    summary.active_version = fsBefore->active_version;

    if (!sm.updateFeatureFlag(car_id, feature, new_flag, err)) {
        summary.result = "failed";
        summary.reason = err.empty() ? "updateFeatureFlag failed" : err;
        errorMessage = summary.reason;
        return false;
    }

    if (sync_config) {
        std::string msg;
        if (syncCarConfigFromState(car_id, msg, true)) {
            summary.config_synced = true;
            summary.result = "success";
            summary.reason.clear();
            return true;
        }
        summary.config_synced = false;
        summary.config_sync_error = msg;
        summary.result = "partial_success";
        summary.reason = msg.empty() ? "config sync failed after flag update" : msg;
        return false;
    }

    summary.config_synced = false;
    summary.result = "success";
    summary.reason.clear();
    return true;
}

void DeploymentManager::printFleetFlagSummary(const FleetFlagSummary& s) const {
    std::cout << "[SUMMARY] action=" << s.action << " car_id=" << s.car_id << " feature=" << s.feature
              << " previous_flag=" << s.previous_flag << " new_flag=" << s.new_flag
              << " active_slot=" << s.active_slot << " active_version=" << s.active_version
              << " sync_config_requested=" << (s.sync_config_requested ? "true" : "false")
              << " config_synced=" << (s.config_synced ? "true" : "false") << " result=" << s.result
              << " reason=" << s.reason;
    if (!s.config_sync_error.empty()) {
        std::cout << " config_sync_error=" << s.config_sync_error;
    }
    std::cout << "\n";
    std::cout << "[NOTE] Feature Flag ON/OFF is administrative state in deploy/state (OFF = normal administrative "
                 "disable). fleet-kill is the separate emergency path (kill_switch + kill_reason in deploy/state).\n";
    if (s.sync_config_requested) {
        std::cout << "[NOTE] " << s.note
                  << " — the per-car .conf feature block follows active_slot/version; the ON/OFF flag may not appear "
                     "as a dedicated line in .conf.\n";
    }
}

std::string DeploymentManager::fleetFlagSummaryToJson(const FleetFlagSummary& s) const {
    std::ostringstream os;
    os << "{";
    os << "\"action\":\"" << jsonEscape(s.action) << "\",";
    os << "\"scope\":\"" << jsonEscape(s.scope) << "\",";
    os << "\"car_id\":\"" << jsonEscape(s.car_id) << "\",";
    os << "\"feature\":\"" << jsonEscape(s.feature) << "\",";
    os << "\"previous_flag\":\"" << jsonEscape(s.previous_flag) << "\",";
    os << "\"new_flag\":\"" << jsonEscape(s.new_flag) << "\",";
    os << "\"active_slot\":\"" << jsonEscape(s.active_slot) << "\",";
    os << "\"active_version\":\"" << jsonEscape(s.active_version) << "\",";
    os << "\"sync_config_requested\":" << (s.sync_config_requested ? "true" : "false") << ",";
    os << "\"config_synced\":" << (s.config_synced ? "true" : "false") << ",";
    os << "\"config_sync_error\":\"" << jsonEscape(s.config_sync_error) << "\",";
    os << "\"result\":\"" << jsonEscape(s.result) << "\",";
    os << "\"reason\":\"" << jsonEscape(s.reason) << "\",";
    os << "\"state_file\":\"" << jsonEscape(s.state_file) << "\",";
    os << "\"config_file\":\"" << jsonEscape(s.config_file) << "\",";
    os << "\"note\":\"" << jsonEscape(s.note) << "\"";
    os << "}";
    return os.str();
}

bool DeploymentManager::fleetKillVehicle(const std::string& car_id,
    const std::string& feature,
    const std::string& kill_reason_in,
    bool sync_config,
    FleetKillSummary& summary,
    std::string& errorMessage) {
    errorMessage.clear();
    summary = FleetKillSummary{};
    summary.car_id = car_id;
    summary.feature = feature;
    summary.sync_config_requested = sync_config;
    summary.note =
        "fleet-kill updates deployment state in deploy/state only; runtime EventBus KillSwitch integration can be "
        "connected later. fleet-flag (FLAG OFF) is normal administrative disable; fleet-kill is emergency disable due "
        "to a detected issue.";

    const std::string reason = trim(kill_reason_in);
    summary.kill_reason = reason;

    if (!isSyncConfigCarId(car_id)) {
        summary.result = "failed";
        summary.reason = "invalid car_id (allowed: car_1 .. car_10): " + car_id;
        errorMessage = summary.reason;
        return false;
    }
    summary.state_file = "deploy/state/" + car_id + ".json";
    summary.config_file = "config/car/" + car_id + ".conf";

    if (!isValidFleetFeature(feature)) {
        summary.result = "failed";
        summary.reason = "invalid feature (expected ADAS|Navigation|Energy): " + feature;
        errorMessage = summary.reason;
        return false;
    }

    if (reason.empty()) {
        summary.result = "failed";
        summary.reason = "kill reason must be non-empty";
        errorMessage = summary.reason;
        return false;
    }

    StateManager sm(workDir_);
    std::string err;
    auto stOpt = sm.loadState(car_id, err);
    if (!stOpt) {
        summary.result = "failed";
        summary.reason = err.empty() ? ("failed to load " + summary.state_file) : err;
        errorMessage = summary.reason;
        return false;
    }

    StateManager::FeatureState* fs = StateManager::featureRef(*stOpt, feature);
    if (!fs) {
        summary.result = "failed";
        summary.reason = "internal: unknown feature: " + feature;
        errorMessage = summary.reason;
        return false;
    }

    summary.previous_slot = fs->active_slot;
    summary.previous_version = fs->active_version;
    summary.previous_flag = fs->flag;

    const std::string fallbackVer = fs->fallback_version;
    auto pickVersion = [&](const char* defaultVer) -> std::string {
        return fallbackVer.empty() ? std::string(defaultVer) : fallbackVer;
    };

    if (feature == "ADAS") {
        summary.policy_note =
            "ADAS kill switch disables the affected feature and falls back to stable slot A (flag OFF; "
            "active_version uses fallback_version when set, else v0.1)";
        fs->kill_switch = true;
        fs->kill_reason = reason;
        fs->flag = "OFF";
        fs->active_slot = "A";
        fs->active_version = pickVersion("v0.1");
        fs->realtime_route_flag.clear();
        fs->charging_recommend_flag.clear();
        fs->advanced_range_flag.clear();
    } else if (feature == "Navigation") {
        summary.policy_note =
            "Navigation keeps core guidance flag ON while disabling realtime_route and charging_recommend (slot A; "
            "active_version uses fallback_version when set, else v1.0)";
        fs->kill_switch = true;
        fs->kill_reason = reason;
        fs->flag = "ON";
        fs->active_slot = "A";
        fs->active_version = pickVersion("v1.0");
        fs->realtime_route_flag = "OFF";
        fs->charging_recommend_flag = "OFF";
        fs->advanced_range_flag.clear();
    } else {
        summary.policy_note =
            "Energy keeps essential battery flag ON while disabling advanced_range and charging_recommend (slot A; "
            "active_version uses fallback_version when set, else v0.5)";
        fs->kill_switch = true;
        fs->kill_reason = reason;
        fs->flag = "ON";
        fs->active_slot = "A";
        fs->active_version = pickVersion("v0.5");
        fs->advanced_range_flag = "OFF";
        fs->charging_recommend_flag = "OFF";
        fs->realtime_route_flag.clear();
    }

    if (!isValidFleetSlot(fs->active_slot)) {
        summary.result = "failed";
        summary.reason = "internal: invalid active_slot after kill policy";
        errorMessage = summary.reason;
        return false;
    }

    if (!sm.saveState(car_id, *stOpt, err)) {
        summary.result = "failed";
        summary.reason = err.empty() ? "saveState failed" : err;
        errorMessage = summary.reason;
        return false;
    }

    summary.new_slot = fs->active_slot;
    summary.new_version = fs->active_version;
    summary.new_flag = fs->flag;
    summary.kill_switch = fs->kill_switch;
    summary.realtime_route_flag = fs->realtime_route_flag;
    summary.charging_recommend_flag = fs->charging_recommend_flag;
    summary.advanced_range_flag = fs->advanced_range_flag;

    if (sync_config) {
        std::string msg;
        if (syncCarConfigFromState(car_id, msg, true)) {
            summary.config_synced = true;
            summary.result = "success";
            summary.reason.clear();
            return true;
        }
        summary.config_synced = false;
        summary.config_sync_error = msg;
        summary.result = "partial_success";
        summary.reason = msg.empty() ? "config sync failed after fleet-kill state update" : msg;
        return false;
    }

    summary.config_synced = false;
    summary.result = "success";
    summary.reason.clear();
    return true;
}

void DeploymentManager::printFleetKillSummary(const FleetKillSummary& s) const {
    std::cout << "[SUMMARY] action=" << s.action << " car_id=" << s.car_id << " feature=" << s.feature
              << " kill_reason=" << s.kill_reason << " previous_slot=" << s.previous_slot
              << " previous_version=" << s.previous_version << " new_slot=" << s.new_slot << " new_version=" << s.new_version
              << " previous_flag=" << s.previous_flag << " new_flag=" << s.new_flag
              << " kill_switch=" << (s.kill_switch ? "true" : "false")
              << " sync_config_requested=" << (s.sync_config_requested ? "true" : "false")
              << " config_synced=" << (s.config_synced ? "true" : "false") << " result=" << s.result << " reason=" << s.reason
              << " policy_note=" << s.policy_note << "\n";
    if (!s.config_sync_error.empty()) {
        std::cout << "[SUMMARY] config_sync_error=" << s.config_sync_error << "\n";
    }
    std::cout << "[NOTE] " << s.note << "\n";
    std::cout << "[NOTE] fleet-kill currently updates deployment state only; runtime EventBus KillSwitch integration can "
                 "be connected later.\n";
    if (s.sync_config_requested) {
        std::cout << "[NOTE] config sync rewrites config/car/<car_id>.conf from deploy/state (active_slot/version lines "
                     "reflect fallback slot A after kill).\n";
    }
}

std::string DeploymentManager::fleetKillSummaryToJson(const FleetKillSummary& s) const {
    std::ostringstream os;
    os << "{";
    os << "\"action\":\"" << jsonEscape(s.action) << "\",";
    os << "\"scope\":\"" << jsonEscape(s.scope) << "\",";
    os << "\"car_id\":\"" << jsonEscape(s.car_id) << "\",";
    os << "\"feature\":\"" << jsonEscape(s.feature) << "\",";
    os << "\"kill_reason\":\"" << jsonEscape(s.kill_reason) << "\",";
    os << "\"previous_slot\":\"" << jsonEscape(s.previous_slot) << "\",";
    os << "\"previous_version\":\"" << jsonEscape(s.previous_version) << "\",";
    os << "\"new_slot\":\"" << jsonEscape(s.new_slot) << "\",";
    os << "\"new_version\":\"" << jsonEscape(s.new_version) << "\",";
    os << "\"previous_flag\":\"" << jsonEscape(s.previous_flag) << "\",";
    os << "\"new_flag\":\"" << jsonEscape(s.new_flag) << "\",";
    os << "\"kill_switch\":" << (s.kill_switch ? "true" : "false") << ",";
    if (s.feature == "Navigation") {
        os << "\"realtime_route_flag\":\"" << jsonEscape(s.realtime_route_flag) << "\",";
        os << "\"charging_recommend_flag\":\"" << jsonEscape(s.charging_recommend_flag) << "\",";
    } else if (s.feature == "Energy") {
        os << "\"advanced_range_flag\":\"" << jsonEscape(s.advanced_range_flag) << "\",";
        os << "\"charging_recommend_flag\":\"" << jsonEscape(s.charging_recommend_flag) << "\",";
    }
    os << "\"sync_config_requested\":" << (s.sync_config_requested ? "true" : "false") << ",";
    os << "\"config_synced\":" << (s.config_synced ? "true" : "false") << ",";
    os << "\"config_sync_error\":\"" << jsonEscape(s.config_sync_error) << "\",";
    os << "\"result\":\"" << jsonEscape(s.result) << "\",";
    os << "\"reason\":\"" << jsonEscape(s.reason) << "\",";
    os << "\"state_file\":\"" << jsonEscape(s.state_file) << "\",";
    os << "\"config_file\":\"" << jsonEscape(s.config_file) << "\",";
    os << "\"policy_note\":\"" << jsonEscape(s.policy_note) << "\",";
    os << "\"note\":\"" << jsonEscape(s.note) << "\"";
    os << "}";
    return os.str();
}

bool DeploymentManager::fleetShadowVehicle(const std::string& car_id,
    const std::string& feature,
    const std::string& shadow_action,
    FleetShadowSummary& summary,
    std::string& errorMessage) {
    errorMessage.clear();
    summary = FleetShadowSummary{};
    summary.car_id = car_id;
    summary.feature = feature;
    summary.shadow_action = shadow_action;
    summary.note =
        "fleet-shadow currently records shadow state in deploy/state; runtime ShadowMirrorData/EventBus integration "
        "can be connected later.";

    if (!isSyncConfigCarId(car_id)) {
        summary.result = "failed";
        summary.reason = "invalid car_id (allowed: car_1 .. car_10): " + car_id;
        errorMessage = summary.reason;
        return false;
    }
    summary.state_file = "deploy/state/" + car_id + ".json";

    if (!isValidFleetFeature(feature)) {
        summary.result = "failed";
        summary.reason = "invalid feature (expected ADAS|Navigation|Energy): " + feature;
        errorMessage = summary.reason;
        return false;
    }

    if (shadow_action != "ON" && shadow_action != "OFF") {
        summary.result = "failed";
        summary.reason = "invalid shadow action (expected ON|OFF): " + shadow_action;
        errorMessage = summary.reason;
        return false;
    }

    StateManager sm(workDir_);
    std::string err;
    const auto stOpt = sm.loadState(car_id, err);
    if (!stOpt) {
        summary.result = "failed";
        summary.reason = err.empty() ? ("failed to load " + summary.state_file) : err;
        errorMessage = summary.reason;
        return false;
    }

    const StateManager::FeatureState* fs = StateManager::featureRef(*stOpt, feature);
    if (!fs) {
        summary.result = "failed";
        summary.reason = "internal: unknown feature: " + feature;
        errorMessage = summary.reason;
        return false;
    }

    summary.previous_shadow_enabled = fs->shadow_enabled;
    summary.previous_shadow_slot = fs->shadow_slot;
    summary.previous_shadow_version = fs->shadow_version;
    summary.active_slot = fs->active_slot;
    summary.active_version = fs->active_version;
    summary.flag = fs->flag;
    summary.kill_switch = fs->kill_switch;

    if (shadow_action == "ON") {
        summary.policy_note =
            "Shadow Mode does not change the active control version; B slot is recorded for comparison only.";
        std::string vmErr;
        const auto vmOpt = VersionManager::loadFromFile(workDir_ + "/deploy/version_map.json", vmErr);
        if (!vmOpt) {
            summary.result = "failed";
            summary.reason = vmErr.empty() ? "failed to load deploy/version_map.json" : vmErr;
            errorMessage = summary.reason;
            return false;
        }
        std::string slotErr;
        const auto infoOpt = vmOpt->getInfo(feature, 'B', slotErr);
        if (!infoOpt || infoOpt->version.empty()) {
            summary.result = "failed";
            summary.reason = slotErr.empty() ? "VersionManager: could not resolve B slot version for feature " + feature
                                               : slotErr;
            errorMessage = summary.reason;
            return false;
        }
        if (!sm.updateShadowState(car_id, feature, true, "B", infoOpt->version, err)) {
            summary.result = "failed";
            summary.reason = err.empty() ? "updateShadowState failed" : err;
            errorMessage = summary.reason;
            return false;
        }
        summary.new_shadow_enabled = true;
        summary.new_shadow_slot = "B";
        summary.new_shadow_version = infoOpt->version;
    } else {
        summary.policy_note = "Shadow Mode disabled; active control version remains unchanged.";
        if (!sm.updateShadowState(car_id, feature, false, "", "", err)) {
            summary.result = "failed";
            summary.reason = err.empty() ? "updateShadowState failed" : err;
            errorMessage = summary.reason;
            return false;
        }
        summary.new_shadow_enabled = false;
        summary.new_shadow_slot.clear();
        summary.new_shadow_version.clear();
    }

    summary.result = "success";
    summary.reason.clear();
    return true;
}

void DeploymentManager::printFleetShadowSummary(const FleetShadowSummary& s) const {
    std::cout << "[SUMMARY] action=" << s.action << " car_id=" << s.car_id << " feature=" << s.feature
              << " shadow_action=" << s.shadow_action
              << " previous_shadow_enabled=" << (s.previous_shadow_enabled ? "true" : "false")
              << " previous_shadow_slot=" << s.previous_shadow_slot
              << " previous_shadow_version=" << s.previous_shadow_version
              << " new_shadow_enabled=" << (s.new_shadow_enabled ? "true" : "false") << " new_shadow_slot=" << s.new_shadow_slot
              << " new_shadow_version=" << s.new_shadow_version << " active_slot=" << s.active_slot
              << " active_version=" << s.active_version << " flag=" << s.flag << " kill_switch=" << (s.kill_switch ? "true" : "false")
              << " result=" << s.result << " reason=" << s.reason << " policy_note=" << s.policy_note << "\n";
    std::cout << "[NOTE] " << s.note << "\n";
}

std::string DeploymentManager::fleetShadowSummaryToJson(const FleetShadowSummary& s) const {
    std::ostringstream os;
    os << "{";
    os << "\"action\":\"" << jsonEscape(s.action) << "\",";
    os << "\"scope\":\"" << jsonEscape(s.scope) << "\",";
    os << "\"car_id\":\"" << jsonEscape(s.car_id) << "\",";
    os << "\"feature\":\"" << jsonEscape(s.feature) << "\",";
    os << "\"shadow_action\":\"" << jsonEscape(s.shadow_action) << "\",";
    os << "\"previous_shadow_enabled\":" << (s.previous_shadow_enabled ? "true" : "false") << ",";
    os << "\"previous_shadow_slot\":\"" << jsonEscape(s.previous_shadow_slot) << "\",";
    os << "\"previous_shadow_version\":\"" << jsonEscape(s.previous_shadow_version) << "\",";
    os << "\"new_shadow_enabled\":" << (s.new_shadow_enabled ? "true" : "false") << ",";
    os << "\"new_shadow_slot\":\"" << jsonEscape(s.new_shadow_slot) << "\",";
    os << "\"new_shadow_version\":\"" << jsonEscape(s.new_shadow_version) << "\",";
    os << "\"active_slot\":\"" << jsonEscape(s.active_slot) << "\",";
    os << "\"active_version\":\"" << jsonEscape(s.active_version) << "\",";
    os << "\"flag\":\"" << jsonEscape(s.flag) << "\",";
    os << "\"kill_switch\":" << (s.kill_switch ? "true" : "false") << ",";
    os << "\"result\":\"" << jsonEscape(s.result) << "\",";
    os << "\"reason\":\"" << jsonEscape(s.reason) << "\",";
    os << "\"state_file\":\"" << jsonEscape(s.state_file) << "\",";
    os << "\"policy_note\":\"" << jsonEscape(s.policy_note) << "\",";
    os << "\"note\":\"" << jsonEscape(s.note) << "\"";
    os << "}";
    return os.str();
}

bool DeploymentManager::fleetPromoteShadowVehicle(const std::string& car_id, const std::string& feature, std::string& outMessage) {
    outMessage.clear();
    if (!isSyncConfigCarId(car_id)) {
        outMessage = "invalid car_id (allowed: car_1 .. car_10): " + car_id;
        return false;
    }
    if (!isValidFleetFeature(feature)) {
        outMessage = "invalid feature (expected ADAS|Navigation|Energy): " + feature;
        return false;
    }

    StateManager sm(workDir_);
    std::string err;
    auto stOpt = sm.loadState(car_id, err);
    if (!stOpt) {
        outMessage = err.empty() ? "failed to load deploy/state/" + car_id + ".json" : err;
        return false;
    }
    auto* fs = StateManager::featureRef(*stOpt, feature);
    if (!fs) {
        outMessage = "internal: unknown feature: " + feature;
        return false;
    }

    const std::string prev_slot = fs->active_slot;
    const std::string prev_ver = fs->active_version;
    const std::string prev_img = fs->primary_image;

    const std::string next_ver = fs->shadow_version;
    const std::string next_slot = fs->shadow_slot;
    std::string next_img = fs->shadow_image;
    if (next_ver.empty()) {
        outMessage = "no shadow_version recorded; run fleet-shadow ON or docker deploy-shadow first";
        return false;
    }

    if (next_img.empty()) {
        const std::string vNoV = stripLeadingVTokenLocal(next_ver);
        if (!vNoV.empty()) {
            const std::string repo = dockerRepoForFleetFeatureLocal(feature);
            next_img = repo + ":v" + vNoV;
        }
    }

    // Promote Shadow -> Primary (metadata only; runtime container actions are external).
    if (!next_slot.empty()) {
        fs->active_slot = next_slot;
        fs->primary_slot = next_slot;
    }
    fs->active_version = next_ver;
    fs->primary_version = next_ver;
    if (!next_img.empty()) {
        fs->primary_image = next_img;
    }

    // Disable shadow lane after promotion.
    fs->shadow_enabled = false;
    fs->shadow_slot.clear();
    fs->shadow_version.clear();
    fs->shadow_image.clear();
    fs->shadow_status = "DISABLED";
    fs->shadow_started_at.clear();

    // Record previous primary selection into rollback_stack (cap 3).
    if (!prev_ver.empty() && prev_ver != next_ver) {
        StateManager::RollbackStackEntry e{};
        e.slot = prev_slot;
        e.version = prev_ver;
        e.docker_image = prev_img;
        e.previous_role = "primary";
        e.recorded_at = nowTimestampIso();
        e.reason = "promoted_from_shadow";
        e.status = "stable";

        // Update in-memory state, then save once below.
        fs->rollback_stack.erase(
            std::remove_if(fs->rollback_stack.begin(), fs->rollback_stack.end(),
                [&](const StateManager::RollbackStackEntry& x) {
                    return x.version == e.version && x.docker_image == e.docker_image;
                }),
            fs->rollback_stack.end());
        fs->rollback_stack.insert(fs->rollback_stack.begin(), e);
        if (fs->rollback_stack.size() > 3) {
            fs->rollback_stack.resize(3);
        }
    }

    if (!sm.saveState(car_id, *stOpt, err)) {
        outMessage = err.empty() ? "failed to save state after promote" : err;
        return false;
    }

    outMessage = "promoted shadow->primary (metadata only) for " + car_id + " " + feature + " to " + next_ver;
    return true;
}

void DeploymentManager::printStatus(const std::string& platformConfigPath) const {
    std::cout << "=== STATUS PATHS ===\n";
    std::cout << "Working directory: " << workDir_ << "\n";
    std::cout << "Selection file:    " << selectionPath_ << "\n";
    std::cout << "Platform config:   " << platformConfigPath << "\n\n";
    printInventory();
}

void DeploymentManager::printInventory() const {
    ConfigUpdater::Selection sel{};
    ConfigUpdater::loadSelection(selectionPath_, sel);

    std::cout << "=== DEPLOYMENT / RELEASE STATUS ===\n";
    std::cout << "candidate=deployed candidate, active=selected version, stable=confirmed stable\n";
    for (const char* domain : kInventoryDomains) {
        InventoryRecord record{};
        loadInventoryRecord(domain, record);
        std::cout << std::left << std::setw(6) << domain
                  << " candidate=" << record.candidateVersion
                  << "   active=" << record.activeVersion
                  << "   stable=" << record.stableVersion
                  << "   status=" << record.lastDeploymentStatus
                  << "   rollback=" << (record.rollbackAvailable ? "yes" : "no");
        if (!record.lastDeployedAt.empty()) {
            std::cout << "   at=" << record.lastDeployedAt;
        }
        std::cout << "\n";
    }
    std::cout << "\n";

    std::cout << "=== CURRENT SELECTION ===\n";
    std::cout << "ADAS=" << sel.adas << "   NAV=" << sel.nav << "   ENERGY=" << sel.energy << "\n\n";

    std::cout << "=== ACTIVE BINARIES ===\n";
    std::error_code ec;
    if (!std::filesystem::exists(activeDir_, ec)) {
        std::cout << "(empty)\n";
    } else {
        bool any = false;
        for (const auto& e : std::filesystem::directory_iterator(activeDir_, ec)) {
            if (e.is_regular_file()) {
                any = true;
                std::cout << e.path().filename().string() << "\n";
            }
        }
        if (!any) {
            std::cout << "(empty)\n";
        }
    }
    std::cout << "\n";

    std::cout << "=== RECENT HISTORY ===\n";
    const auto recentHistory = history_.readRecent(5);
    if (recentHistory.empty()) {
        std::cout << "(no history yet)\n";
    } else {
        for (const auto& line : recentHistory) {
            std::cout << line << "\n";
        }
    }
    std::cout << "\n";

    std::cout << "=== RECENT MANIFEST ===\n";
    std::ifstream in(manifestPath_);
    if (!in) {
        std::cout << "(no manifest yet)\n";
        return;
    }
    std::string line;
    std::vector<std::string> tail;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            tail.push_back(line);
        }
    }
    constexpr std::size_t kMax = 5;
    if (tail.size() > kMax) {
        tail.erase(tail.begin(), tail.end() - static_cast<std::ptrdiff_t>(kMax));
    }
    for (const auto& l : tail) {
        std::cout << l << "\n";
    }
}

bool DeploymentManager::applyPendingNativeForCar(const std::string& car_id, std::string& outMessage) {
    outMessage.clear();
    std::string err;
    StateManager sm(workDir_);
    auto stateOpt = sm.loadState(car_id, err);
    if (!stateOpt) {
        outMessage = err.empty() ? "loadState failed" : err;
        return false;
    }
    const auto vmOpt = VersionManager::loadFromFile(workDir_ + "/deploy/version_map.json", err);
    if (!vmOpt) {
        outMessage = err.empty() ? "version_map load failed" : err;
        return false;
    }

    static const char* kFeatures[] = {"ADAS", "Navigation", "Energy"};
    std::vector<std::string> binariesOrdered;
    std::vector<std::string> featuresWithPending;
    for (const char* feat : kFeatures) {
        const StateManager::FeatureState* fs = StateManager::featureRef(*stateOpt, feat);
        if (!fs || fs->pending_active_version.empty()) {
            continue;
        }
        std::string vr;
        if (!validateTargetVersion(*vmOpt, feat, fs->pending_active_slot, fs->pending_active_version, vr)) {
            outMessage = vr;
            return false;
        }
        const char slotCh = fs->pending_active_slot.empty() ? 'A' : fs->pending_active_slot[0];
        const auto bin = vmOpt->getBinary(feat, slotCh, err);
        if (!bin) {
            outMessage = err.empty() ? "getBinary failed" : err;
            return false;
        }
        if (std::find(binariesOrdered.begin(), binariesOrdered.end(), *bin) == binariesOrdered.end()) {
            binariesOrdered.push_back(*bin);
        }
        featuresWithPending.push_back(feat);
    }

    if (featuresWithPending.empty()) {
        outMessage = "no pending native rollout";
        return true;
    }

    for (const std::string& bin : binariesOrdered) {
        if (!deployExecutable(bin)) {
            outMessage = "deployExecutable failed for " + bin;
            return false;
        }
    }

    for (const std::string& feat : featuresWithPending) {
        // Pending apply is the real "active/primary changes" point for deferred native rollout.
        // Record rollback_stack here (not at fleet-deploy scheduling time).
        std::string prev_slot;
        std::string prev_version;
        std::string prev_image;
        {
            auto stPrev = sm.loadState(car_id, err);
            if (stPrev) {
                const auto* fsPrev = StateManager::featureRef(*stPrev, feat);
                if (fsPrev) {
                    prev_slot = fsPrev->active_slot;
                    prev_version = fsPrev->active_version;
                    prev_image = fsPrev->primary_image;
                }
            }
            err.clear();
        }
        if (!sm.promotePendingToActive(car_id, feat, err)) {
            outMessage = err.empty() ? "promotePendingToActive failed" : err;
            return false;
        }

        // Push previous primary into rollback_stack only when the active version actually changed.
        std::string new_version;
        {
            auto stAfter = sm.loadState(car_id, err);
            if (stAfter) {
                const auto* fsAfter = StateManager::featureRef(*stAfter, feat);
                if (fsAfter) {
                    new_version = fsAfter->active_version;
                }
            }
            err.clear();
        }
        if (!prev_version.empty() && !new_version.empty() && prev_version != new_version) {
            StateManager::RollbackStackEntry e{};
            e.slot = prev_slot;
            e.version = prev_version;
            e.docker_image = prev_image;
            e.previous_role = "primary";
            e.recorded_at = nowTimestampIso();
            e.reason = "applied_pending";
            e.status = "stable";
            std::string rberr;
            (void)sm.pushRollbackStackEntry(car_id, feat, e, 3, rberr);
        }
    }

    outMessage = "applied pending native for " + car_id;
    return true;
}

}  // namespace sdv::platform::deployment

#pragma once

#include <optional>
#include <string>

namespace sdv::platform::lifecyclemgr {

/// Resolved fleet version metadata for logging (does not drive binary execution).
struct FleetDomainRuntimeInfo {
    std::string fleet_label;   // ADAS | Navigation | Energy
    std::string slot;
    std::string version;
    std::string mode;          // primary | shadow
    std::string enabled;       // ON | OFF
    std::string image;
    std::string artifact_type;  // optional (from deploy/state when present)
    std::string package_id;     // optional (from deploy/state when present)
    std::string binary_path;
    std::string source;        // config | state | vehicle_runtime | unknown
};

/// Per-domain versions from `deploy/vehicle_runtime/<car_id>.json`.
struct VehicleRuntimeDomainVersions {
    std::string json_path;
    std::string domain_key;
    std::string fleet_label;
    std::string primary_version;
    std::string shadow_version;
    bool found = false;
};

/// Best-effort path to `deploy/vehicle_runtime/<car_id>.json` (empty if not found).
std::string resolveVehicleRuntimeJsonPath(const std::string& carId);

/// Reads `domains.<domain>.primary_version` / `shadow_version` for a fleet feature process.
std::optional<VehicleRuntimeDomainVersions> readVehicleRuntimeDomainVersions(const std::string& processName,
    const std::string& carId);

/// Display token without a leading `v` (e.g. `v4.0` → `4.0`).
std::string stripLeadingVForDisplay(std::string version);

/// CMD-friendly line listing both lane versions from vehicle_runtime JSON.
std::string formatVehicleRuntimeVersionLogLine(const VehicleRuntimeDomainVersions& rt,
    const std::string& carId,
    const std::string& processName,
    bool shadowProcess);

/// Maps a spawned process name (e.g. AdasFeature) to fleet metadata using vehicle_runtime → config → state.
std::optional<FleetDomainRuntimeInfo> resolveFleetVersionForProcess(const std::string& processName,
    const std::string& carId,
    const std::string& lifecycleConfigPath);

std::string formatVersionLogLine(const FleetDomainRuntimeInfo& info, const std::string& carId);

/// e.g. [ADAS] started car_id=car_1 version=v1.0 slot=B mode=primary enabled=ON source=config
std::string formatFeatureStartedLogLine(const FleetDomainRuntimeInfo& info, const std::string& carId);

/// Best-effort append to logs/run_sdv/<car_id>.log (parent or child).
void appendRunSdvVersionLog(const std::string& carId, const std::string& line);

bool isFleetFeatureProcessName(const std::string& processName);

/// [SKIP] line when \p shouldSpawnFleetProcess returns false.
std::string formatFleetSpawnSkipLogLine(const FleetDomainRuntimeInfo& info,
    const std::string& carId,
    const std::string& processName);

/// Whether LifeCycleMgr should fork/spawn this fleet-managed feature process.
/// When false, \p outLine is set to a [SKIP] message; when ignore override applies, \p outWarnLine is set.
bool shouldSpawnFleetProcess(const std::string& processName,
    const std::string& carId,
    const std::string& lifecycleConfigPath,
    std::string& outLine,
    std::string& outWarnLine);

/// Append to logs/run_sdv/<car_id>.log and logs/<logRootDir>/<Process>.log (best-effort).
void appendFleetSpawnGateLog(const std::string& carId,
    const std::string& logRootDir,
    const std::string& processName,
    const std::string& line);

}  // namespace sdv::platform::lifecyclemgr

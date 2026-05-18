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
    std::string source;        // config | state | runtime | unknown
};

/// Maps a spawned process name (e.g. AdasFeature) to fleet metadata using config → state → (optional) runtime.
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

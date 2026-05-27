#pragma once

#include <chrono>
#include <cstddef>
#include <string>

namespace sdv::platform::lifecyclemgr {

struct TelemetryCollectorTuning {
    std::size_t batchMax{50};
    int flushMs{1000};
};

/// Defaults from env or built-in constants.
TelemetryCollectorTuning resolveTelemetryTuningFromEnv();

/// <repo>/config/telemetry/runtime/<carId>.json (or SDV_TELEMETRY_RUNTIME_CONFIG).
std::string resolveTelemetryRuntimeConfigPath(const std::string& carId);

/// DataCollector: reload batch_max / flush_ms when JSON mtime changes.
bool tryReloadTelemetryRuntimeTuning(const std::string& configPath,
                                     TelemetryCollectorTuning& current,
                                     std::chrono::system_clock::time_point& lastReadTime);

/// Feature loop: publish interval ms (reloads JSON periodically).
int resolveTelemetryPublishIntervalMs(const std::string& featureName);

/// Blocks for the configured publish interval (EventBus telemetry cadence).
void sleepTelemetryPublishInterval(const std::string& featureName);

}  // namespace sdv::platform::lifecyclemgr

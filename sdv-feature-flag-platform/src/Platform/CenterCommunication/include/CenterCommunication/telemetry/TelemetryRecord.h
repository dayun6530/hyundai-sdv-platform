#pragma once

#include <optional>
#include <string>

namespace sdv::platform::centercommunication {

/// Normalized telemetry event produced by Feature::sendTelemetry (EventBus TelemetryData message).
struct TelemetryRecord {
    std::string rawJson;
    std::string carId;
    std::string feature;
    std::string eventType;
    std::string level;
    std::string role;
    std::string timestamp;

    static TelemetryRecord fromRaw(const std::string& rawJson, const std::string& fallbackCarId);
    static std::optional<TelemetryRecord> tryParse(const std::string& rawJson,
        const std::string& fallbackCarId);
};

}  // namespace sdv::platform::centercommunication

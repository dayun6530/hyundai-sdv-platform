#include "CenterCommunication/telemetry/TelemetryRecord.h"

#include <nlohmann/json.hpp>

namespace sdv::platform::centercommunication {

namespace {

static std::string getStringField(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

}  // namespace

TelemetryRecord TelemetryRecord::fromRaw(const std::string& rawJson, const std::string& fallbackCarId) {
    if (auto parsed = tryParse(rawJson, fallbackCarId)) {
        return *parsed;
    }
    TelemetryRecord r;
    r.rawJson = rawJson;
    r.carId = fallbackCarId;
    return r;
}

std::optional<TelemetryRecord> TelemetryRecord::tryParse(const std::string& rawJson,
    const std::string& fallbackCarId) {
    try {
        const nlohmann::json j = nlohmann::json::parse(rawJson);
        TelemetryRecord r;
        r.rawJson = rawJson;
        r.carId = getStringField(j, "car_id");
        if (r.carId.empty()) {
            r.carId = fallbackCarId;
        }
        r.feature = getStringField(j, "feature");
        r.eventType = getStringField(j, "event_type");
        r.level = getStringField(j, "level");
        r.role = getStringField(j, "role");
        r.timestamp = getStringField(j, "timestamp");
        return r;
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
}

}  // namespace sdv::platform::centercommunication

#include "CenterCommunication/telemetry/OtlpLogsExporter.h"

#include "CenterCommunication/telemetry/HttpPostJson.h"

#include <cstdio>
#include <ctime>
#include <string>

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

static void addStringAttribute(nlohmann::json& attributes, const std::string& key, const std::string& value) {
    if (key.empty() || value.empty()) {
        return;
    }
    attributes.push_back({
        {"key", key},
        {"value", {{"stringValue", value}}},
    });
}

static int severityNumberFromLevel(const std::string& level) {
    if (level == "TRACE") {
        return 1;
    }
    if (level == "DEBUG") {
        return 5;
    }
    if (level == "INFO") {
        return 9;
    }
    if (level == "WARN" || level == "WARNING") {
        return 13;
    }
    if (level == "ERROR") {
        return 17;
    }
    if (level == "FATAL") {
        return 21;
    }
    return 0;
}

static std::string iso8601UtcToUnixNano(const std::string& timestamp) {
    if (timestamp.size() < 20) {
        return {};
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(timestamp.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute,
            &second) != 6) {
        return {};
    }

    std::size_t pos = 19;
    std::uint64_t fractionalNanos = 0;
    if (pos < timestamp.size() && timestamp[pos] == '.') {
        ++pos;
        int digits = 0;
        while (pos < timestamp.size() && timestamp[pos] >= '0' && timestamp[pos] <= '9') {
            if (digits < 9) {
                fractionalNanos =
                    fractionalNanos * 10 + static_cast<std::uint64_t>(timestamp[pos] - '0');
                ++digits;
            }
            ++pos;
        }
        while (digits > 0 && digits < 9) {
            fractionalNanos *= 10;
            ++digits;
        }
    }

    if (pos >= timestamp.size() || timestamp[pos] != 'Z') {
        return {};
    }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = 0;

    const std::time_t seconds = timegm(&tm);
    if (seconds < 0) {
        return {};
    }

    const std::uint64_t unixNanos =
        static_cast<std::uint64_t>(seconds) * 1000000000ULL + fractionalNanos;
    return std::to_string(unixNanos);
}

static std::string wrapAsOtlpLogJson(const TelemetryRecord& record) {
    nlohmann::json telemetry = nlohmann::json::object();
    try {
        telemetry = nlohmann::json::parse(record.rawJson);
    } catch (const nlohmann::json::parse_error&) {
        // Preserve the original body even if a producer emits malformed telemetry.
    }

    const std::string parsedCarId = getStringField(telemetry, "car_id");
    const std::string effectiveCarId = parsedCarId.empty() ? record.carId : parsedCarId;
    const std::string level = record.level.empty() ? getStringField(telemetry, "level") : record.level;
    const std::string timestampField =
        record.timestamp.empty() ? getStringField(telemetry, "timestamp") : record.timestamp;
    const std::string timestampUnixNano = iso8601UtcToUnixNano(timestampField);

    nlohmann::json resourceAttributes = nlohmann::json::array();
    addStringAttribute(resourceAttributes, "service.name", "sdv-data-collector");
    addStringAttribute(resourceAttributes, "car_id", effectiveCarId);

    nlohmann::json logAttributes = nlohmann::json::array();
    const std::string feature =
        record.feature.empty() ? getStringField(telemetry, "feature") : record.feature;
    const std::string eventType =
        record.eventType.empty() ? getStringField(telemetry, "event_type") : record.eventType;
    const std::string role = record.role.empty() ? getStringField(telemetry, "role") : record.role;
    addStringAttribute(logAttributes, "feature", feature);
    addStringAttribute(logAttributes, "event_type", eventType);
    addStringAttribute(logAttributes, "level", level);
    addStringAttribute(logAttributes, "role", role);

    nlohmann::json logRecord = {
        {"body", {{"stringValue", record.rawJson}}},
    };
    if (!timestampUnixNano.empty()) {
        logRecord["timeUnixNano"] = timestampUnixNano;
    }
    if (!level.empty()) {
        logRecord["severityText"] = level;
        const int severityNumber = severityNumberFromLevel(level);
        if (severityNumber > 0) {
            logRecord["severityNumber"] = severityNumber;
        }
    }
    if (!logAttributes.empty()) {
        logRecord["attributes"] = logAttributes;
    }

    nlohmann::json body = {
        {"resourceLogs",
            {{
                {"resource", {{"attributes", resourceAttributes}}},
                {"scopeLogs",
                    {{
                        {"scope", {{"name", "sdv-data-collector"}}},
                        {"logRecords", {logRecord}},
                    }}},
            }}},
    };
    return body.dump();
}

}  // namespace

OtlpLogsExporter::OtlpLogsExporter(std::string otlpLogsEndpoint, std::string defaultCarId)
    : otlpLogsEndpoint_(std::move(otlpLogsEndpoint))
    , defaultCarId_(std::move(defaultCarId)) {}

bool OtlpLogsExporter::exportRecord(const TelemetryRecord& record) {
    TelemetryRecord effective = record;
    if (effective.carId.empty()) {
        effective.carId = defaultCarId_;
    }
    if (effective.rawJson.empty()) {
        return true;
    }

    const std::string body = wrapAsOtlpLogJson(effective);
    std::string lastError;
    return httpPostJsonWithRetry(otlpLogsEndpoint_, body, "OtlpLogsExporter", &lastError);
}

}  // namespace sdv::platform::centercommunication

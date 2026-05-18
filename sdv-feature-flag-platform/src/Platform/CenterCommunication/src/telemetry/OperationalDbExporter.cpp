#include "CenterCommunication/telemetry/OperationalDbExporter.h"

#include "CenterCommunication/telemetry/HttpPostJson.h"

#include <iostream>

#include <nlohmann/json.hpp>

namespace sdv::platform::centercommunication {

OperationalDbExporter::OperationalDbExporter(std::string ingestUrl, std::string defaultCarId)
    : ingestUrl_(std::move(ingestUrl))
    , defaultCarId_(std::move(defaultCarId)) {}

std::string OperationalDbExporter::buildIngestBody(const TelemetryRecord& record) const {
    nlohmann::json body = {
        {"vehicle_id", record.carId.empty() ? defaultCarId_ : record.carId},
        {"feature", record.feature},
        {"event_type", record.eventType},
        {"level", record.level},
        {"role", record.role},
        {"occurred_at", record.timestamp},
        {"raw", nlohmann::json::parse(record.rawJson, nullptr, false)},
    };
    if (body["raw"].is_discarded()) {
        body["raw"] = record.rawJson;
    }
    return body.dump();
}

bool OperationalDbExporter::exportRecord(const TelemetryRecord& record) {
    if (ingestUrl_.empty()) {
        return true;
    }
    if (record.rawJson.empty()) {
        return true;
    }

    const std::string body = buildIngestBody(record);
    std::string lastError;
    return httpPostJsonWithRetry(ingestUrl_, body, "OperationalDbExporter", &lastError);
}

}  // namespace sdv::platform::centercommunication

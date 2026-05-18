#include "CenterCommunication/telemetry/OperationalDbExporter.h"

#include "CenterCommunication/telemetry/HttpPostJson.h"

#include <iostream>

#include <nlohmann/json.hpp>

namespace sdv::platform::centercommunication {

namespace {

nlohmann::json eventObjectFromRecord(const TelemetryRecord& record, const std::string& defaultCarId) {
    nlohmann::json body = {
        {"vehicle_id", record.carId.empty() ? defaultCarId : record.carId},
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
    return body;
}

}  // namespace

OperationalDbExporter::OperationalDbExporter(std::string ingestUrl, std::string defaultCarId)
    : ingestUrl_(std::move(ingestUrl))
    , batchIngestUrl_(resolveBatchIngestUrl(ingestUrl_))
    , defaultCarId_(std::move(defaultCarId)) {}

std::string OperationalDbExporter::resolveBatchIngestUrl(const std::string& ingestUrl) {
    constexpr const char* kIngestSuffix = "/v1/ingest";
    if (ingestUrl.size() >= 11 &&
        ingestUrl.compare(ingestUrl.size() - 11, 11, kIngestSuffix) == 0) {
        return ingestUrl + "/batch";
    }
    if (!ingestUrl.empty() && ingestUrl.back() == '/') {
        return ingestUrl + "v1/ingest/batch";
    }
    return ingestUrl + "/batch";
}

std::string OperationalDbExporter::buildBatchIngestBody(
    const std::vector<TelemetryRecord>& records) const {
    nlohmann::json events = nlohmann::json::array();
    for (const TelemetryRecord& record : records) {
        if (record.rawJson.empty()) {
            continue;
        }
        events.push_back(eventObjectFromRecord(record, defaultCarId_));
    }
    return nlohmann::json{{"events", events}}.dump();
}

bool OperationalDbExporter::postBatch(const std::vector<TelemetryRecord>& records) {
    if (batchIngestUrl_.empty()) {
        return true;
    }

    std::vector<TelemetryRecord> nonEmpty;
    nonEmpty.reserve(records.size());
    for (const TelemetryRecord& record : records) {
        if (!record.rawJson.empty()) {
            nonEmpty.push_back(record);
        }
    }
    if (nonEmpty.empty()) {
        return true;
    }

    const nlohmann::json events = nlohmann::json::parse(buildBatchIngestBody(nonEmpty)).at("events");
    if (!events.is_array() || events.empty()) {
        return true;
    }

    const std::string body = nlohmann::json{{"events", events}}.dump();
    std::string lastError;
    return httpPostJsonWithRetry(batchIngestUrl_, body, "OperationalDbExporter(batch)", &lastError);
}

bool OperationalDbExporter::exportRecord(const TelemetryRecord& record) {
    return exportRecords({record});
}

bool OperationalDbExporter::exportRecords(const std::vector<TelemetryRecord>& records) {
    if (records.empty()) {
        return true;
    }
    if (records.size() == 1 && !ingestUrl_.empty() && !records.front().rawJson.empty()) {
        const std::string body = eventObjectFromRecord(records.front(), defaultCarId_).dump();
        std::string lastError;
        return httpPostJsonWithRetry(ingestUrl_, body, "OperationalDbExporter", &lastError);
    }
    return postBatch(records);
}

bool OperationalDbExporter::flush() {
    return true;
}

}  // namespace sdv::platform::centercommunication

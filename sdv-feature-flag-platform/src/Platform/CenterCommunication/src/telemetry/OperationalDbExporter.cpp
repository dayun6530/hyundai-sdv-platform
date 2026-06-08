#include "CenterCommunication/telemetry/OperationalDbExporter.h"

#include "CenterCommunication/telemetry/HttpPostJson.h"

#include <iostream>

#include <nlohmann/json.hpp>

namespace sdv::platform::centercommunication {

namespace {

nlohmann::json eventObjectFromRecord(const TelemetryRecord& record, const std::string& defaultCarId) {
    nlohmann::json raw = nlohmann::json::parse(record.rawJson, nullptr, false);
    if (raw.is_discarded()) {
        raw = record.rawJson;
    }

    nlohmann::json body = {
        {"vehicle_id", record.carId.empty() ? defaultCarId : record.carId},
        {"feature", record.feature},
        {"event_type", record.eventType},
        {"level", record.level},
        {"role", record.role},
        {"occurred_at", record.timestamp},
        {"raw", raw},
    };

    // Mirror Grafana/Loki body: store inner payload separately for SQL/jsonb queries.
    if (raw.is_object()) {
        const auto payloadIt = raw.find("payload");
        if (payloadIt != raw.end()) {
            body["payload"] = *payloadIt;
        }
        const auto tsIt = raw.find("timestamp");
        if (tsIt != raw.end() && tsIt->is_string() && record.timestamp.empty()) {
            body["occurred_at"] = tsIt->get<std::string>();
        }
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
    if (httpPostJsonWithRetry(batchIngestUrl_, body, "OperationalDbExporter(batch)", &lastError)) {
        return true;
    }

    // Batch failed (HTTP, validation rollback, or oversized body on raw-socket transport).
    // Fall back to per-event ingest so Loki/Grafana and Postgres stay aligned.
    bool allOk = true;
    for (const TelemetryRecord& record : nonEmpty) {
        if (record.rawJson.empty()) {
            continue;
        }
        const std::string singleBody = eventObjectFromRecord(record, defaultCarId_).dump();
        std::string singleError;
        if (!httpPostJsonWithRetry(ingestUrl_, singleBody, "OperationalDbExporter", &singleError)) {
            allOk = false;
        }
    }
    return allOk;
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

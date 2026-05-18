#pragma once

#include "CenterCommunication/telemetry/ITelemetryExporter.h"

#include <cstddef>
#include <string>

namespace sdv::platform::centercommunication {

/// Posts normalized telemetry to operational data ingest API (single or batch).
class OperationalDbExporter : public ITelemetryExporter {
public:
    OperationalDbExporter(std::string ingestUrl, std::string defaultCarId);

    const char* name() const override { return "operational_db"; }
    bool exportRecord(const TelemetryRecord& record) override;
    bool exportRecords(const std::vector<TelemetryRecord>& records) override;
    bool flush() override;

private:
    static std::string resolveBatchIngestUrl(const std::string& ingestUrl);
    std::string buildBatchIngestBody(const std::vector<TelemetryRecord>& records) const;
    bool postBatch(const std::vector<TelemetryRecord>& records);

    std::string ingestUrl_;
    std::string batchIngestUrl_;
    std::string defaultCarId_;
};

}  // namespace sdv::platform::centercommunication

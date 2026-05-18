#pragma once

#include "CenterCommunication/telemetry/TelemetryRecord.h"

#include <vector>

namespace sdv::platform::centercommunication {

/// Export sink for telemetry records (OTLP, operational DB ingest, etc.).
class ITelemetryExporter {
public:
    virtual ~ITelemetryExporter() = default;

    virtual const char* name() const = 0;
    virtual bool exportRecord(const TelemetryRecord& record) = 0;

    /// Export multiple records (operational DB uses HTTP batch ingest).
    virtual bool exportRecords(const std::vector<TelemetryRecord>& records);

    /// Flush any buffered records (no-op for exporters without buffering).
    virtual bool flush();
};

}  // namespace sdv::platform::centercommunication

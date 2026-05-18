#pragma once

#include "CenterCommunication/telemetry/TelemetryRecord.h"

namespace sdv::platform::centercommunication {

/// Export sink for telemetry records (OTLP, operational DB ingest, etc.).
class ITelemetryExporter {
public:
    virtual ~ITelemetryExporter() = default;

    virtual const char* name() const = 0;
    virtual bool exportRecord(const TelemetryRecord& record) = 0;
};

}  // namespace sdv::platform::centercommunication

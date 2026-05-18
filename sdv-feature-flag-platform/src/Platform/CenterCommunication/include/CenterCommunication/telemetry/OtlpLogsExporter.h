#pragma once

#include "CenterCommunication/telemetry/ITelemetryExporter.h"

#include <string>

namespace sdv::platform::centercommunication {

/// Wraps telemetry JSON as OTLP logs JSON and POSTs to an OTLP HTTP endpoint.
class OtlpLogsExporter : public ITelemetryExporter {
public:
    OtlpLogsExporter(std::string otlpLogsEndpoint, std::string defaultCarId);

    const char* name() const override { return "otlp_logs"; }
    bool exportRecord(const TelemetryRecord& record) override;

private:
    std::string otlpLogsEndpoint_;
    std::string defaultCarId_;
};

}  // namespace sdv::platform::centercommunication

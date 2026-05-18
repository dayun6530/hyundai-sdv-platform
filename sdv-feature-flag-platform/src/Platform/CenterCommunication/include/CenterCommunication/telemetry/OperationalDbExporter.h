#pragma once

#include "CenterCommunication/telemetry/ITelemetryExporter.h"

#include <string>

namespace sdv::platform::centercommunication {

/// Posts normalized telemetry to an operational data ingest API (implementation pending).
class OperationalDbExporter : public ITelemetryExporter {
public:
    OperationalDbExporter(std::string ingestUrl, std::string defaultCarId);

    const char* name() const override { return "operational_db"; }
    bool exportRecord(const TelemetryRecord& record) override;

private:
    std::string buildIngestBody(const TelemetryRecord& record) const;

    std::string ingestUrl_;
    std::string defaultCarId_;
};

}  // namespace sdv::platform::centercommunication

#include "CenterCommunication/telemetry/ITelemetryExporter.h"

namespace sdv::platform::centercommunication {

bool ITelemetryExporter::exportRecords(const std::vector<TelemetryRecord>& records) {
    bool ok = true;
    for (const TelemetryRecord& record : records) {
        if (!exportRecord(record)) {
            ok = false;
        }
    }
    return ok;
}

bool ITelemetryExporter::flush() {
    return true;
}

}  // namespace sdv::platform::centercommunication

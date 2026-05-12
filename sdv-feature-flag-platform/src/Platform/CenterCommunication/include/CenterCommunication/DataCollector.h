#pragma once

#include <string>
#include <atomic>

namespace sdv::platform::centercommunication {

class DataCollector {
public:
    DataCollector(const std::string& eventBusAddress,
                  const std::string& otlpLogsEndpoint,
                  const std::string& carId);

    void run();
    void stop();

private:
    bool exportTelemetryPayload(const std::string& telemetryJson);

    std::string eventBusAddress_;
    std::string otlpLogsEndpoint_;
    std::string carId_;
    std::atomic<bool> running_;
};

}  // namespace sdv::platform::centercommunication

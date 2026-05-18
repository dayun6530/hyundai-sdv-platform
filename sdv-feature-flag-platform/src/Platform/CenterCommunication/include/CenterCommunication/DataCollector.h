#pragma once

#include "CenterCommunication/detail/BoundedQueue.h"
#include "CenterCommunication/telemetry/ITelemetryExporter.h"
#include "CenterCommunication/telemetry/TelemetryRecord.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace sdv::platform::centercommunication {

class DataCollector {
public:
    static constexpr std::size_t kDefaultQueueCapacity = 4096;

    DataCollector(const std::string& eventBusAddress,
                  const std::string& otlpLogsEndpoint,
                  const std::string& carId,
                  std::size_t queueCapacity = kDefaultQueueCapacity);

    DataCollector(const std::string& eventBusAddress,
                  std::vector<std::unique_ptr<ITelemetryExporter>> exporters,
                  const std::string& carId,
                  std::size_t queueCapacity = kDefaultQueueCapacity);

    ~DataCollector();

    void run();
    void stop();

private:
    static std::vector<std::unique_ptr<ITelemetryExporter>> buildDefaultExporters(
        const std::string& otlpLogsEndpoint,
        const std::string& carId);

    void exportWorkerLoop();
    void dispatchTelemetry(const std::string& telemetryJson);
    void dispatchTelemetryBatch(const std::vector<TelemetryRecord>& records);
    void flushExporters();

    std::string eventBusAddress_;
    std::string carId_;
    std::size_t queueCapacity_;
    std::vector<std::unique_ptr<ITelemetryExporter>> exporters_;
    detail::BoundedQueue<std::string> telemetryQueue_;
    std::thread exporterThread_;
    std::atomic<bool> running_;
};

}  // namespace sdv::platform::centercommunication

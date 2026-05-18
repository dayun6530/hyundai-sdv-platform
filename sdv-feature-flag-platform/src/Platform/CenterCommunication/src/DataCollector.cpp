#include "CenterCommunication/DataCollector.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <signal.h>
#include <thread>

#include "CenterCommunication/telemetry/OperationalDbExporter.h"
#include "CenterCommunication/telemetry/OtlpLogsExporter.h"
#include "CenterCommunication/telemetry/TelemetryRecord.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

namespace sdv::platform::centercommunication {

namespace {

std::string resolveOperationalDbIngestUrl() {
    if (const char* env = std::getenv("SDV_OPERATIONAL_DB_INGEST_URL")) {
        if (env[0] != '\0') {
            return std::string(env);
        }
    }
    return "http://127.0.0.1:8090/v1/ingest";
}

}  // namespace

std::vector<std::unique_ptr<ITelemetryExporter>> DataCollector::buildDefaultExporters(
    const std::string& otlpLogsEndpoint,
    const std::string& carId) {
    std::vector<std::unique_ptr<ITelemetryExporter>> exporters;
    exporters.push_back(std::make_unique<OtlpLogsExporter>(otlpLogsEndpoint, carId));
    exporters.push_back(
        std::make_unique<OperationalDbExporter>(resolveOperationalDbIngestUrl(), carId));
    return exporters;
}

DataCollector::DataCollector(const std::string& eventBusAddress,
                             const std::string& otlpLogsEndpoint,
                             const std::string& carId,
                             const std::size_t queueCapacity)
    : DataCollector(eventBusAddress, buildDefaultExporters(otlpLogsEndpoint, carId), carId, queueCapacity) {
}

DataCollector::DataCollector(const std::string& eventBusAddress,
                             std::vector<std::unique_ptr<ITelemetryExporter>> exporters,
                             const std::string& carId,
                             const std::size_t queueCapacity)
    : eventBusAddress_(eventBusAddress)
    , carId_(carId)
    , queueCapacity_(queueCapacity > 0 ? queueCapacity : kDefaultQueueCapacity)
    , exporters_(std::move(exporters))
    , telemetryQueue_(queueCapacity_)
    , running_(false) {}

DataCollector::~DataCollector() {
    stop();
    telemetryQueue_.close();
    if (exporterThread_.joinable()) {
        exporterThread_.join();
    }
}

void DataCollector::dispatchTelemetry(const std::string& telemetryJson) {
    const TelemetryRecord record = TelemetryRecord::fromRaw(telemetryJson, carId_);
    for (const auto& exporter : exporters_) {
        if (!exporter) {
            continue;
        }
        if (!exporter->exportRecord(record)) {
            std::cerr << "[DataCollector] exporter '" << exporter->name() << "' failed" << std::endl;
        }
    }
}

void DataCollector::exportWorkerLoop() {
    while (true) {
        std::string payload;
        if (telemetryQueue_.pop(payload, std::chrono::milliseconds(200))) {
            dispatchTelemetry(payload);
            continue;
        }
        if (telemetryQueue_.isClosed()) {
            break;
        }
    }

    std::string payload;
    while (telemetryQueue_.tryPop(payload)) {
        dispatchTelemetry(payload);
    }
}

void DataCollector::run() {
    running_ = true;
    exporterThread_ = std::thread(&DataCollector::exportWorkerLoop, this);

    sdv::platform::communications::eventbus::EventBusProxy proxy(eventBusAddress_);
    const int64_t cid = proxy.registerComponent();

    if (cid < 0) {
        std::cerr << "[DataCollector] FATAL: EventBus registerComponent failed (address="
                  << eventBusAddress_ << ")" << std::endl;
        running_ = false;
        telemetryQueue_.close();
        if (exporterThread_.joinable()) {
            exporterThread_.join();
        }
        return;
    }

    std::cout << "[DataCollector] Started. EventBus=" << eventBusAddress_
              << " component_id=" << cid
              << " carId=" << carId_
              << " queueCapacity=" << queueCapacity_
              << " exporters=" << exporters_.size();
    for (const auto& exporter : exporters_) {
        if (exporter) {
            std::cout << " " << exporter->name();
        }
    }
    std::cout << std::endl;

    while (running_.load(std::memory_order_acquire)) {
        auto q = proxy.getEventQueue(cid);
        while (q.getSize() > 0) {
            auto ev = q.getEvent();
            if (ev.getEventId() == sdv::platform::communications::eventbus::EventId::TelemetryData) {
                telemetryQueue_.push(ev.getMessage());
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    telemetryQueue_.close();
    if (exporterThread_.joinable()) {
        exporterThread_.join();
    }

    proxy.unRegister(cid);

    const std::uint64_t drops = telemetryQueue_.dropCount();
    if (drops > 0) {
        std::cerr << "[DataCollector] NOTE: dropped " << drops
                  << " telemetry item(s) while queue was at capacity (" << queueCapacity_
                  << ")" << std::endl;
    }
    std::cout << "[DataCollector] Stopped." << std::endl;
}

void DataCollector::stop() {
    running_.store(false, std::memory_order_release);
}

}  // namespace sdv::platform::centercommunication

static std::atomic<bool> shutdownRequested(false);

void dataCollectorSignalHandler(int) {
    shutdownRequested = true;
}

#if !defined(SDV_CENTERCOMM_NO_MAIN)
static std::string resolveEventBusAddressFromEnvOrDefault() {
    if (const char* env = std::getenv("SDV_EVENTBUS_ADDR"); env != nullptr && *env != '\0') {
        return std::string(env);
    }
    return "127.0.0.1:50051";
}

static std::string resolveOtlpLogsEndpointFromEnvOrDefault() {
    if (const char* env = std::getenv("SDV_OTLP_LOGS_ENDPOINT"); env != nullptr && *env != '\0') {
        return std::string(env);
    }
    return "http://127.0.0.1:4318/v1/logs";
}

static std::string resolveCarIdFromArgvOrEnv(int argc, char** argv) {
    if (argc > 1 && argv[argc - 1]) {
        const std::string last = std::string(argv[argc - 1]);
        if (last.rfind("car_", 0) == 0) {
            return last;
        }
    }
    if (const char* env = std::getenv("SDV_CAR_INSTANCE"); env != nullptr && *env != '\0') {
        return std::string(env);
    }
    return "car_1";
}

int main(int argc, char** argv) {
    signal(SIGTERM, dataCollectorSignalHandler);
    signal(SIGINT, dataCollectorSignalHandler);

    std::string eventBusAddr = resolveEventBusAddressFromEnvOrDefault();
    if (argc > 1 && argv[1] && *argv[1] != '\0') {
        eventBusAddr = std::string(argv[1]);
    }

    const std::string otlpLogsEndpoint = resolveOtlpLogsEndpointFromEnvOrDefault();
    const std::string carId = resolveCarIdFromArgvOrEnv(argc, argv);
    sdv::platform::centercommunication::DataCollector collector(eventBusAddr, otlpLogsEndpoint, carId);

    std::thread collectorThread([&collector]() { collector.run(); });

    while (!shutdownRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    collector.stop();
    collectorThread.join();

    return 0;
}
#endif


#include "CenterCommunication/DataCollector.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <signal.h>
#include <thread>

#include "CenterCommunication/telemetry/OperationalDbExporter.h"
#include "CenterCommunication/telemetry/OtlpLogsExporter.h"
#include "CenterCommunication/telemetry/TelemetryRecord.h"

#include <vector>
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

std::size_t resolveTelemetryBatchMax() {
    constexpr std::size_t kDefault = 50;
    constexpr std::size_t kMin = 1;
    constexpr std::size_t kMax = 100;
    if (const char* env = std::getenv("SDV_TELEMETRY_BATCH_MAX")) {
        try {
            const long parsed = std::strtol(env, nullptr, 10);
            if (parsed >= static_cast<long>(kMin) && parsed <= static_cast<long>(kMax)) {
                return static_cast<std::size_t>(parsed);
            }
        } catch (...) {
        }
    }
    return kDefault;
}

int resolveTelemetryFlushMs() {
    constexpr int kDefault = 1000;
    constexpr int kMin = 500;
    constexpr int kMax = 2000;
    if (const char* env = std::getenv("SDV_TELEMETRY_FLUSH_MS")) {
        try {
            const long parsed = std::strtol(env, nullptr, 10);
            if (parsed >= kMin && parsed <= kMax) {
                return static_cast<int>(parsed);
            }
        } catch (...) {
        }
    }
    return kDefault;
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
    dispatchTelemetryBatch({TelemetryRecord::fromRaw(telemetryJson, carId_)});
}

void DataCollector::dispatchTelemetryBatch(const std::vector<TelemetryRecord>& records) {
    if (records.empty()) {
        return;
    }
    for (const auto& exporter : exporters_) {
        if (!exporter) {
            continue;
        }
        if (!exporter->exportRecords(records)) {
            std::cerr << "[DataCollector] exporter '" << exporter->name() << "' batch export failed"
                      << std::endl;
        }
    }
}

void DataCollector::flushExporters() {
    for (const auto& exporter : exporters_) {
        if (!exporter) {
            continue;
        }
        if (!exporter->flush()) {
            std::cerr << "[DataCollector] exporter '" << exporter->name() << "' flush failed"
                      << std::endl;
        }
    }
}

void DataCollector::exportWorkerLoop() {
    const std::size_t batchMax = resolveTelemetryBatchMax();
    const auto flushInterval = std::chrono::milliseconds(resolveTelemetryFlushMs());

    std::vector<std::string> pending;
    pending.reserve(batchMax);

    auto flushPending = [this, &pending]() {
        if (pending.empty()) {
            return;
        }
        std::vector<TelemetryRecord> records;
        records.reserve(pending.size());
        for (const std::string& payload : pending) {
            records.push_back(TelemetryRecord::fromRaw(payload, carId_));
        }
        dispatchTelemetryBatch(records);
        pending.clear();
    };

    while (true) {
        std::string payload;
        if (telemetryQueue_.pop(payload, flushInterval)) {
            pending.push_back(std::move(payload));
            if (pending.size() >= batchMax) {
                flushPending();
            }
            continue;
        }

        flushPending();

        if (telemetryQueue_.isClosed()) {
            break;
        }
    }

    std::string payload;
    while (telemetryQueue_.tryPop(payload)) {
        pending.push_back(std::move(payload));
        if (pending.size() >= batchMax) {
            flushPending();
        }
    }
    flushPending();
    flushExporters();
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
              << " batchMax=" << resolveTelemetryBatchMax()
              << " flushMs=" << resolveTelemetryFlushMs()
              << " exporters=" << exporters_.size();
    for (const auto& exporter : exporters_) {
        if (exporter) {
            std::cout << " " << exporter->name();
        }
    }
    std::cout << std::endl;

    proxy.consumeEventStream(
        cid,
        [this]() { return running_.load(std::memory_order_acquire); },
        [this](const sdv::platform::communications::eventbus::Event& ev) {
            if (ev.getEventId() == sdv::platform::communications::eventbus::EventId::TelemetryData) {
                telemetryQueue_.push(ev.getMessage());
            }
        });

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


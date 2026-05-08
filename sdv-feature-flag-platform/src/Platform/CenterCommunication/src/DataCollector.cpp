#include "CenterCommunication/DataCollector.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>

#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

namespace fs = std::filesystem;

namespace sdv::platform::centercommunication {

DataCollector::DataCollector(const std::string& eventBusAddress,
                             const std::string& outDir,
                             int64_t maxBatchBytes,
                             int flushIntervalSeconds)
    : eventBusAddress_(eventBusAddress)
    , outDir_(outDir)
    , maxBatchBytes_(maxBatchBytes)
    , flushIntervalSeconds_(flushIntervalSeconds)
    , running_(false) {}

void DataCollector::run() {
    running_ = true;
    fs::create_directories(outDir_);
    sdv::platform::communications::eventbus::EventBusProxy proxy(eventBusAddress_);
    const int64_t cid = proxy.registerComponent();

    {
        std::lock_guard<std::mutex> lock(mu_);
        lastFlush_ = std::chrono::steady_clock::now();
    }

    std::cout << "[DataCollector] Started. EventBus=" << eventBusAddress_
              << " component_id=" << cid
              << " outDir=" << outDir_
              << " maxBatch=" << (maxBatchBytes_ / 1024) << "KB"
              << " flushInterval=" << flushIntervalSeconds_ << "s"
              << std::endl;

    while (running_) {
        auto now = std::chrono::steady_clock::now();

        // Poll EventBus for new events (pull model).
        auto q = proxy.getEventQueue(cid);
        while (q.getSize() > 0) {
            auto ev = q.getEvent();
            if (ev.getEventId() == sdv::platform::communications::eventbus::EventId::TelemetryData) {
                pushTelemetryLine(ev.getMessage());
            }
        }

        // Time-based flush.
        if (shouldFlush(now)) {
            std::lock_guard<std::mutex> lock(mu_);
            flushLocked(now);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Final flush.
    {
        std::lock_guard<std::mutex> lock(mu_);
        flushLocked(std::chrono::steady_clock::now());
    }
    proxy.unRegister(cid);
    std::cout << "[DataCollector] Stopped." << std::endl;
}

void DataCollector::stop() {
    running_ = false;
}

void DataCollector::pushTelemetryLine(const std::string& line) {
    // Thread-safe buffer append.
    std::lock_guard<std::mutex> lock(mu_);
    buffer_.push_back(line);
    bufferBytes_ += static_cast<int64_t>(line.size() + 1); // newline

    // Size-based flush.
    const auto now = std::chrono::steady_clock::now();
    if (bufferBytes_ >= maxBatchBytes_) {
        flushLocked(now);
    }
}

bool DataCollector::shouldFlush(std::chrono::steady_clock::time_point now) const {
    std::lock_guard<std::mutex> lock(mu_);
    if (buffer_.empty()) {
        return false;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastFlush_).count();
    return elapsed >= flushIntervalSeconds_;
}

static std::string nowUtcCompact() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return os.str();
}

void DataCollector::flushLocked(std::chrono::steady_clock::time_point now) {
    if (buffer_.empty()) {
        lastFlush_ = now;
        return;
    }

    const fs::path outDirPath(outDir_);
    std::error_code ec;
    if (!fs::exists(outDirPath, ec) || !fs::is_directory(outDirPath, ec)) {
        // Users may delete the telemetry folder while the process is running.
        // Re-create it defensively right before opening the flush file.
        fs::create_directories(outDirPath, ec);
        if (ec) {
            std::cerr << "[DataCollector] ERROR: failed to create outDir=" << outDir_
                      << " (" << ec.message() << ")\n";
            return;
        }
    }

    const std::string file = (outDirPath / ("telemetry_" + nowUtcCompact() + ".jsonl")).string();
    std::ofstream out(file, std::ios::out | std::ios::app);
    if (out.is_open()) {
        for (const auto& line : buffer_) {
            out << line << "\n";
        }
        out.close();
    } else {
        std::cerr << "[DataCollector] ERROR: failed to open " << file << " for flush\n";
    }

    buffer_.clear();
    bufferBytes_ = 0;
    lastFlush_ = now;
}

}  // namespace sdv::platform::centercommunication

static std::atomic<bool> shutdownRequested(false);

void dataCollectorSignalHandler(int) {
    shutdownRequested = true;
}

static std::string resolveEventBusAddressFromEnvOrDefault() {
    if (const char* env = std::getenv("SDV_EVENTBUS_ADDR"); env != nullptr && *env != '\0') {
        return std::string(env);
    }
    return "127.0.0.1:50051";
}

#if !defined(SDV_CENTERCOMM_NO_MAIN)
int main(int argc, char** argv) {
    signal(SIGTERM, dataCollectorSignalHandler);
    signal(SIGINT, dataCollectorSignalHandler);

    const char* envLogDir = std::getenv("SDV_LOG_DIR");
    const std::string logDir = (envLogDir != nullptr && *envLogDir != '\0')
        ? std::string(envLogDir)
        : std::string("logs");

    std::string eventBusAddr = resolveEventBusAddressFromEnvOrDefault();
    if (argc > 1 && argv[1] && *argv[1] != '\0') {
        eventBusAddr = std::string(argv[1]);
    }

    const std::string outDir = (fs::path(logDir) / "telemetry").string();
    sdv::platform::centercommunication::DataCollector collector(
        eventBusAddr, outDir, 512 * 1024, 10);

    std::thread collectorThread([&collector]() {
        collector.run();
    });

    while (!shutdownRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    collector.stop();
    collectorThread.join();

    return 0;
}
#endif

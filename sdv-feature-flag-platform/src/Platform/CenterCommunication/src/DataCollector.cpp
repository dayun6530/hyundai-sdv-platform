#include "CenterCommunication/DataCollector.h"

#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <signal.h>

namespace fs = std::filesystem;

namespace sdv::platform::centercommunication {

DataCollector::DataCollector(const std::string& logDir,
                             int64_t maxSizeBytes,
                             int intervalSeconds)
    : logDir_(logDir)
    , maxSizeBytes_(maxSizeBytes)
    , intervalSeconds_(intervalSeconds)
    , running_(false) {}

void DataCollector::run() {
    running_ = true;
    std::cout << "[DataCollector] Started. Monitoring: " << logDir_
              << " (maxSize:" << maxSizeBytes_ / 1024 << "KB"
              << ", interval:" << intervalSeconds_ << "s)" << std::endl;

    while (running_) {
        scanLogFiles();

        for (auto& info : logFiles_) {
            if (shouldTransfer(info)) {
                transferToControlCenter(info);
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds_));
    }

    std::cout << "[DataCollector] Stopped." << std::endl;
}

void DataCollector::stop() {
    running_ = false;
}

void DataCollector::scanLogFiles() {
    logFiles_.clear();

    if (!fs::exists(logDir_)) return;

    for (const auto& entry : fs::directory_iterator(logDir_)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".log") continue;

        LogFileInfo info;
        info.filePath = entry.path().string();
        info.sizeBytes = static_cast<int64_t>(entry.file_size());
        info.lastTransferTime = 0;
        logFiles_.push_back(info);
    }
}

bool DataCollector::shouldTransfer(const LogFileInfo& info) const {
    return info.sizeBytes > maxSizeBytes_;
}

void DataCollector::transferToControlCenter(const LogFileInfo& info) {
    std::cout << "[DataCollector] Transfer ready: " << info.filePath
              << " (" << info.sizeBytes / 1024 << "KB)" << std::endl;
    // TODO: FTP/HTTP transfer to Control Center
}

}  // namespace sdv::platform::centercommunication

static std::atomic<bool> shutdownRequested(false);

void dataCollectorSignalHandler(int) {
    shutdownRequested = true;
}

int main() {
    signal(SIGTERM, dataCollectorSignalHandler);
    signal(SIGINT, dataCollectorSignalHandler);

    sdv::platform::centercommunication::DataCollector collector(
        "logs", 512 * 1024, 10);

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

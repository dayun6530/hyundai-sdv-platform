#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <chrono>

namespace sdv::platform::centercommunication {

class DataCollector {
public:
    DataCollector(const std::string& eventBusAddress,
                  const std::string& outDir,
                  int64_t maxBatchBytes,
                  int flushIntervalSeconds);

    void run();
    void stop();

private:
    void pushTelemetryLine(const std::string& line);
    bool shouldFlush(std::chrono::steady_clock::time_point now) const;
    void flushLocked(std::chrono::steady_clock::time_point now);

    std::string eventBusAddress_;
    std::string outDir_;
    int64_t maxBatchBytes_;
    int flushIntervalSeconds_;
    std::atomic<bool> running_;

    mutable std::mutex mu_;
    std::vector<std::string> buffer_;
    int64_t bufferBytes_{0};
    std::chrono::steady_clock::time_point lastFlush_{};
};

}  // namespace sdv::platform::centercommunication

#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>

namespace sdv::platform::centercommunication {

struct LogFileInfo {
    std::string filePath;
    int64_t sizeBytes;
    int64_t lastTransferTime;
};

class DataCollector {
public:
    DataCollector(const std::string& logDir, int64_t maxSizeBytes, int intervalSeconds);

    void run();
    void stop();

private:
    void scanLogFiles();
    bool shouldTransfer(const LogFileInfo& info) const;
    void transferToControlCenter(const LogFileInfo& info);

    std::string logDir_;
    int64_t maxSizeBytes_;
    int intervalSeconds_;
    std::vector<LogFileInfo> logFiles_;
    std::atomic<bool> running_;
};

}  // namespace sdv::platform::centercommunication

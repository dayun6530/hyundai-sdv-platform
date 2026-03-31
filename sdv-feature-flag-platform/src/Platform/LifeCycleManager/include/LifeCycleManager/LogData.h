#pragma once

#include <string>
#include <vector>
#include <chrono>

namespace sdv::platform::lifecyclemgr {

struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    std::string level;
    std::string message;
};

class LogData {
public:
    explicit LogData(const std::string& featureName);

    void addEntry(const std::string& level, const std::string& message);
    const std::vector<LogEntry>& getEntries() const;
    int getEntryCount() const;
    void clear();

    const std::string& getFeatureName() const;

private:
    std::string featureName_;
    std::vector<LogEntry> entries_;
};

}  // namespace sdv::platform::lifecyclemgr

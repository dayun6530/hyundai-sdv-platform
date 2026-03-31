#include "LifeCycleManager/LogData.h"

namespace sdv::platform::lifecyclemgr {

LogData::LogData(const std::string& featureName)
    : featureName_(featureName) {}

void LogData::addEntry(const std::string& level, const std::string& message) {
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = level;
    entry.message = message;
    entries_.push_back(entry);
}

const std::vector<LogEntry>& LogData::getEntries() const {
    return entries_;
}

int LogData::getEntryCount() const {
    return static_cast<int>(entries_.size());
}

void LogData::clear() {
    entries_.clear();
}

const std::string& LogData::getFeatureName() const {
    return featureName_;
}

}  // namespace sdv::platform::lifecyclemgr

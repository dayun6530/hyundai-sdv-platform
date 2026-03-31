#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <mutex>

namespace sdv::platform::centercommunication {

struct SignalData {
    std::string type;
    std::string target;
    std::string payload;
    std::chrono::system_clock::time_point timestamp;

    std::string serialize() const;
    static SignalData deserialize(const std::string& line);
};

class Signal {
public:
    Signal(const std::string& centerUrl, const std::string& signalFilePath);
    virtual ~Signal();

    virtual void start() = 0;
    virtual void stop() = 0;
    bool isRunning() const;

protected:
    // Protocol abstraction (HTTP for now, replaceable with other protocols)
    bool httpPost(const std::string& endpoint, const std::string& body);
    std::string httpGet(const std::string& endpoint);

    void writeToFile(const SignalData& data);
    std::vector<SignalData> readAndClearFile();

    std::string centerUrl_;
    std::string signalFilePath_;
    std::atomic<bool> running_;
    std::mutex fileMutex_;
};

}  // namespace sdv::platform::centercommunication

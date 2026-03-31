#include "CenterCommunication/Signal.h"

#include <iostream>
#include <fstream>
#include <sstream>

namespace sdv::platform::centercommunication {

std::string SignalData::serialize() const {
    return type + ":" + target + ":" + payload;
}

SignalData SignalData::deserialize(const std::string& line) {
    SignalData data;
    data.timestamp = std::chrono::system_clock::now();

    auto first = line.find(':');
    if (first == std::string::npos) {
        data.type = line;
        return data;
    }
    data.type = line.substr(0, first);

    auto second = line.find(':', first + 1);
    if (second == std::string::npos) {
        data.target = line.substr(first + 1);
        return data;
    }
    data.target = line.substr(first + 1, second - first - 1);
    data.payload = line.substr(second + 1);

    return data;
}

Signal::Signal(const std::string& centerUrl, const std::string& signalFilePath)
    : centerUrl_(centerUrl)
    , signalFilePath_(signalFilePath)
    , running_(false) {}

Signal::~Signal() {}

bool Signal::isRunning() const {
    return running_;
}

bool Signal::httpPost(const std::string& endpoint, const std::string& body) {
    // TODO: Replace with real HTTP client (libcurl, cpp-httplib, etc.)
    std::cout << "[Signal] HTTP POST " << centerUrl_ << endpoint
              << " body=" << body << std::endl;
    return true;
}

std::string Signal::httpGet(const std::string& endpoint) {
    // TODO: Replace with real HTTP client
    std::cout << "[Signal] HTTP GET " << centerUrl_ << endpoint << std::endl;
    return "";
}

void Signal::writeToFile(const SignalData& data) {
    std::lock_guard<std::mutex> lock(fileMutex_);
    std::ofstream out(signalFilePath_, std::ios::app);
    if (out.is_open()) {
        out << data.serialize() << std::endl;
    }
}

std::vector<SignalData> Signal::readAndClearFile() {
    std::lock_guard<std::mutex> lock(fileMutex_);
    std::vector<SignalData> result;

    std::ifstream in(signalFilePath_);
    if (!in.is_open()) return result;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        result.push_back(SignalData::deserialize(line));
    }
    in.close();

    if (!result.empty()) {
        std::ofstream clear(signalFilePath_, std::ios::trunc);
    }

    return result;
}

}  // namespace sdv::platform::centercommunication

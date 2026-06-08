#include "CenterCommunication/Signal.h"

#include "SdvSpacedConsole.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <sys/stat.h>

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

namespace {

static std::string basenameOfPath(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

static std::string resolveSignalPath(const std::string& requested) {
    // Backward-compatible: many call sites pass "logs/<file>". When SDV_LOG_DIR is set
    // (e.g. logs/car_1), keep the file name but relocate to the per-car log root.
    static constexpr const char kLogsPrefix[] = "logs/";
    if (requested.rfind(kLogsPrefix, 0) != 0) {
        return requested;
    }

    const char* envLogDir = std::getenv("SDV_LOG_DIR");
    const std::string logDir = (envLogDir != nullptr && *envLogDir != '\0')
        ? std::string(envLogDir)
        : std::string("logs");

    return logDir + "/" + basenameOfPath(requested);
}

static void ensureLogDirsExist(const std::string& path) {
    // Minimal nested dir support for logs/car_<n>/.
    mkdir("logs", 0755);
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return;
    const std::string dir = path.substr(0, slash);
    if (dir != "logs") {
        mkdir(dir.c_str(), 0755);
    }
}

}  // namespace

Signal::Signal(const std::string& centerUrl, const std::string& signalFilePath)
    : centerUrl_(centerUrl)
    , signalFilePath_(resolveSignalPath(signalFilePath))
    , running_(false) {
    ensureLogDirsExist(signalFilePath_);
}

Signal::~Signal() {}

bool Signal::isRunning() const {
    return running_;
}

bool Signal::httpPost(const std::string& endpoint, const std::string& body) {
    // TODO: Replace with real HTTP client (libcurl, cpp-httplib, etc.)
    SDV_SPC_COUT << "[Signal] HTTP POST " << centerUrl_ << endpoint
              << " body=" << body << std::endl;
    return true;
}

std::string Signal::httpGet(const std::string& endpoint) {
    // TODO: Replace with real HTTP client
    SDV_SPC_COUT << "[Signal] HTTP GET " << centerUrl_ << endpoint << std::endl;
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

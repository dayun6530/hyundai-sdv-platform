#include "CenterCommunication/SignalReceiver.h"

#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <cstdlib>

namespace sdv::platform::centercommunication {

namespace {

static bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
        s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string resolveExternalSignalsPath() {
    // Highest priority: explicitly set by orchestrator.
    if (const char* envPath = std::getenv("SDV_EXTERNAL_SIGNALS");
        envPath != nullptr && *envPath != '\0') {
        return std::string(envPath);
    }

    // Convenience fallback for multi-car demo: infer from per-car log dir.
    // (This prevents silent mis-routing when SDV_EXTERNAL_SIGNALS wasn't propagated.)
    if (const char* envLogDir = std::getenv("SDV_LOG_DIR");
        envLogDir != nullptr && *envLogDir != '\0') {
        const std::string d(envLogDir);
        if (endsWith(d, "/A") || endsWith(d, "\\A")) return "config/ExternalSignals_A.cmd";
        if (endsWith(d, "/B") || endsWith(d, "\\B")) return "config/ExternalSignals_B.cmd";
    }

    // Backward-compatible default.
    return "config/ExternalSignals.cmd";
}

}  // namespace

SignalReceiver::SignalReceiver(const std::string& centerUrl,
                               const std::string& signalFilePath)
    : Signal(centerUrl, signalFilePath) {}

void SignalReceiver::start() {
    running_ = true;
    std::cout << "[SignalReceiver] Started. Center: " << centerUrl_
              << " -> " << signalFilePath_ << std::endl;
    std::cout << "[SignalReceiver] External cmd: " << resolveExternalSignalsPath() << std::endl;

    while (running_) {
        pollExternalSignals();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    std::cout << "[SignalReceiver] Stopped." << std::endl;
}

void SignalReceiver::stop() {
    running_ = false;
}

void SignalReceiver::pollExternalSignals() {
    // TODO: Replace with real HTTP polling from center
    // httpGet("/api/signals") → parse → writeToFile()
    //
    // For prototype: reads from a command file shared with the operator/terminal.
    // IMPORTANT: when running multiple platforms (car_A + car_B) concurrently,
    // each must read its own input file to avoid cross-talk.
    const std::string externalPath = resolveExternalSignalsPath();

    std::ifstream input(externalPath);
    if (!input.is_open()) return;

    std::string line;
    bool hasSignals = false;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;

        SignalData data = SignalData::deserialize(line);
        data.timestamp = std::chrono::system_clock::now();

        std::cout << "[SignalReceiver] Received: " << data.serialize() << std::endl;
        writeToFile(data);
        hasSignals = true;
    }
    input.close();

    if (hasSignals) {
        std::ofstream clear(externalPath, std::ios::trunc);
    }
}

}  // namespace sdv::platform::centercommunication

static std::atomic<bool> shutdownRequested(false);

void signalReceiverHandler(int) {
    shutdownRequested = true;
}

int main() {
    signal(SIGTERM, signalReceiverHandler);
    signal(SIGINT, signalReceiverHandler);

    sdv::platform::centercommunication::SignalReceiver receiver;

    std::thread receiverThread([&receiver]() {
        receiver.start();
    });

    while (!shutdownRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    receiver.stop();
    receiverThread.join();

    return 0;
}

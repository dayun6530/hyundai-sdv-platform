#include "CenterCommunication/SignalReceiver.h"

#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <cstdlib>

namespace sdv::platform::centercommunication {

namespace {

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
        // Generalized: logs/<carId> -> config/signals/ExternalSignals_<carId>.cmd
        // Example: logs/car_1 -> config/signals/ExternalSignals_car_1.cmd
        const std::size_t slash = d.find_last_of("/\\");
        const std::string tail = (slash == std::string::npos) ? d : d.substr(slash + 1);
        if (!tail.empty() && tail != "logs") {
            return "config/signals/ExternalSignals_" + tail + ".cmd";
        }
    }

    // Default single-instance fallback.
    return "config/signals/ExternalSignals.cmd";
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
    // IMPORTANT: when running multiple platforms (car_1..car_10) concurrently,
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

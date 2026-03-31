#include "CenterCommunication/SignalReceiver.h"

#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <signal.h>

namespace sdv::platform::centercommunication {

SignalReceiver::SignalReceiver(const std::string& centerUrl,
                               const std::string& signalFilePath)
    : Signal(centerUrl, signalFilePath) {}

void SignalReceiver::start() {
    running_ = true;
    std::cout << "[SignalReceiver] Started. Center: " << centerUrl_
              << " -> " << signalFilePath_ << std::endl;

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
    // For prototype: reads from config/ExternalSignals.cmd
    std::ifstream input("config/ExternalSignals.cmd");
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
        std::ofstream clear("config/ExternalSignals.cmd", std::ios::trunc);
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

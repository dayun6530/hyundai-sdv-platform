#include "CenterCommunication/SignalSender.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>

namespace sdv::platform::centercommunication {

SignalSender::SignalSender(const std::string& centerUrl,
                           const std::string& signalFilePath)
    : Signal(centerUrl, signalFilePath) {}

void SignalSender::start() {
    running_ = true;
    std::cout << "[SignalSender] Started. " << signalFilePath_
              << " -> Center: " << centerUrl_ << std::endl;

    while (running_) {
        processOutboundSignals();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    std::cout << "[SignalSender] Stopped." << std::endl;
}

void SignalSender::stop() {
    running_ = false;
}

void SignalSender::processOutboundSignals() {
    auto signals = readAndClearFile();

    for (const auto& data : signals) {
        sendToCenter(data);
    }
}

void SignalSender::sendToCenter(const SignalData& data) {
    std::cout << "[SignalSender] Sending to center: " << data.serialize() << std::endl;
    httpPost("/api/signals", data.serialize());
}

}  // namespace sdv::platform::centercommunication

static std::atomic<bool> shutdownRequested(false);

void signalSenderHandler(int) {
    shutdownRequested = true;
}

int main() {
    signal(SIGTERM, signalSenderHandler);
    signal(SIGINT, signalSenderHandler);

    sdv::platform::centercommunication::SignalSender sender;

    std::thread senderThread([&sender]() {
        sender.start();
    });

    while (!shutdownRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    sender.stop();
    senderThread.join();

    return 0;
}

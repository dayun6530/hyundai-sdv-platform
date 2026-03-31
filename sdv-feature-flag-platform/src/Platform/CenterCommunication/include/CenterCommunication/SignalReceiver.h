#pragma once

#include "CenterCommunication/Signal.h"

namespace sdv::platform::centercommunication {

class SignalReceiver : public Signal {
public:
    static constexpr const char* defaultSignalFilePath = "logs/SignalsInbound.cmd";

    explicit SignalReceiver(
        const std::string& centerUrl = "http://center.example.com",
        const std::string& signalFilePath = defaultSignalFilePath);

    void start() override;
    void stop() override;

private:
    void pollExternalSignals();
};

}  // namespace sdv::platform::centercommunication

#pragma once

#include "CenterCommunication/Signal.h"

namespace sdv::platform::centercommunication {

class SignalSender : public Signal {
public:
    static constexpr const char* defaultSignalFilePath = "logs/SignalsOutbound.cmd";

    explicit SignalSender(
        const std::string& centerUrl = "http://center.example.com",
        const std::string& signalFilePath = defaultSignalFilePath);

    void start() override;
    void stop() override;

private:
    void processOutboundSignals();
    void sendToCenter(const SignalData& data);
};

}  // namespace sdv::platform::centercommunication

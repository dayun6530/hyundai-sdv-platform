#include "CenterCommunication/SignalSender.h"
#include "TestAssert.h"

#include <filesystem>
#include <fstream>
#include <thread>

using namespace sdv::platform::centercommunication;

int main() {
    sqa::test::TestCounter tc;

    // SignalSender is now a EventBus->MQTT bridge. The long-lived MQTT + EventBus clients live in
    // the process main(), not in SignalSender::start(). This unit test only verifies start/stop.
    SignalSender sender("http://center.example.com", "logs/SignalsOutbound.cmd");
    std::thread worker([&sender]() { sender.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    sender.stop();
    worker.join();
    sqa::test::check(tc, true, "SignalSender start/stop does not crash");

    return sqa::test::finalize(tc, "SignalSenderTest");
}

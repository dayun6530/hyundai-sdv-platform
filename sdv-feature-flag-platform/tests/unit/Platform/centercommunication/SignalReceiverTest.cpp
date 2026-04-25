#include "CenterCommunication/SignalReceiver.h"
#include "TestAssert.h"

#include <filesystem>
#include <fstream>
#include <thread>

using namespace sdv::platform::centercommunication;

int main() {
    sqa::test::TestCounter tc;

    // SignalReceiver is now a MQTT->EventBus bridge. The long-lived MQTT client lives in the
    // process main(), not in SignalReceiver::start(). This unit test only verifies start/stop.
    SignalReceiver receiver("http://center.example.com", "logs/SignalsInbound.cmd");
    std::thread worker([&receiver]() { receiver.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    receiver.stop();
    worker.join();
    sqa::test::check(tc, true, "SignalReceiver start/stop does not crash");

    return sqa::test::finalize(tc, "SignalReceiverTest");
}

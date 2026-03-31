#include "CenterCommunication/SignalReceiver.h"
#include "TestAssert.h"

#include <filesystem>
#include <fstream>
#include <thread>

#define main signalreceiver_original_main
#include "src/Platform/CenterCommunication/src/SignalReceiver.cpp"
#undef main

using namespace sdv::platform::centercommunication;

int main() {
    sqa::test::TestCounter tc;
    namespace fs = std::filesystem;
    fs::create_directories("config");
    fs::create_directories("tests/mockup/generated");

    const std::string inboundFile = "tests/mockup/generated/SignalReceiverInbound.cmd";
    std::ofstream(inboundFile, std::ios::trunc).close();

    {
        std::ofstream ext("config/ExternalSignals.cmd", std::ios::trunc);
        ext << "# comment\n";
        ext << "FLAG_ON:AdasFeature:on\n";
        ext << "KILL:NavigationFeature:kill\n";
    }

    SignalReceiver receiver("http://center.example.com", inboundFile);
    std::thread worker([&receiver]() { receiver.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(3400));
    receiver.stop();
    worker.join();

    std::ifstream in(inboundFile);
    std::string line;
    int rowCount = 0;
    while (std::getline(in, line)) {
        if (!line.empty()) rowCount++;
    }
    sqa::test::check(tc, rowCount >= 2, "SignalReceiver writes received signals to inbound file");

    std::ifstream extAfter("config/ExternalSignals.cmd");
    std::string remaining;
    std::getline(extAfter, remaining);
    sqa::test::check(tc, remaining.empty(), "SignalReceiver clears external signal source file");

    return sqa::test::finalize(tc, "SignalReceiverTest");
}

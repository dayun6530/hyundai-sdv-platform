#include "CenterCommunication/SignalSender.h"
#include "TestAssert.h"

#include <filesystem>
#include <fstream>
#include <thread>

#define main signalsender_original_main
#include "src/Platform/CenterCommunication/src/SignalSender.cpp"
#undef main

using namespace sdv::platform::centercommunication;

int main() {
    sqa::test::TestCounter tc;
    namespace fs = std::filesystem;
    fs::create_directories("tests/mockup/generated");

    const std::string outboundFile = "tests/mockup/generated/SignalSenderOutbound.cmd";
    {
        std::ofstream out(outboundFile, std::ios::trunc);
        out << "FLAG_ON:AdasFeature:on\n";
        out << "KILL:NavigationFeature:kill\n";
    }

    SignalSender sender("http://center.example.com", outboundFile);
    std::thread worker([&sender]() { sender.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(2400));
    sender.stop();
    worker.join();

    std::ifstream in(outboundFile);
    std::string line;
    std::getline(in, line);
    sqa::test::check(tc, line.empty(), "SignalSender reads and clears outbound signal file");

    return sqa::test::finalize(tc, "SignalSenderTest");
}

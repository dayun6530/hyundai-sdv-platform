#define private public
#include "CenterCommunication/DataCollector.h"
#undef private
#include "TestAssert.h"

#include <filesystem>
#include <fstream>
#include <thread>

#define main datacollector_original_main
#include "src/Platform/CenterCommunication/src/DataCollector.cpp"
#undef main

using namespace sdv::platform::centercommunication;

int main() {
    sqa::test::TestCounter tc;
    namespace fs = std::filesystem;
    fs::create_directories("tests/mockup/generated/logs");

    const std::string logDir = "tests/mockup/generated/logs";
    {
        std::ofstream(logDir + "/small.log", std::ios::trunc) << "12345";
        std::ofstream(logDir + "/big.log", std::ios::trunc) << std::string(200, 'x');
        std::ofstream(logDir + "/ignore.txt", std::ios::trunc) << "x";
    }

    DataCollector collector(logDir, 100, 1);
    collector.scanLogFiles();
    sqa::test::check(tc, collector.logFiles_.size() == 2, "scanLogFiles picks only .log files");

    bool hasBigTransfer = false;
    bool hasSmallSkip = false;
    for (const auto& info : collector.logFiles_) {
        if (info.filePath.find("big.log") != std::string::npos) {
            hasBigTransfer = collector.shouldTransfer(info);
            collector.transferToControlCenter(info);  // no-throw path
        } else if (info.filePath.find("small.log") != std::string::npos) {
            hasSmallSkip = !collector.shouldTransfer(info);
        }
    }
    sqa::test::check(tc, hasBigTransfer, "shouldTransfer returns true for large log");
    sqa::test::check(tc, hasSmallSkip, "shouldTransfer returns false for small log");

    std::thread worker([&collector]() { collector.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    collector.stop();
    worker.join();
    sqa::test::check(tc, true, "run/stop lifecycle executes without exception");

    return sqa::test::finalize(tc, "DataCollectorTest");
}

#include "CenterCommunication/DataCollector.h"
#include "TestAssert.h"

#include <thread>

using namespace sdv::platform::centercommunication;

int main() {
    sqa::test::TestCounter tc;
    // EventBus may or may not be running in unit test env. The proxy prints errors on failure but should not throw.
    DataCollector collector("127.0.0.1:50051", "tests/mockup/generated/telemetry_out", 16 * 1024, 1);
    std::thread worker([&collector]() { collector.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    collector.stop();
    worker.join();
    sqa::test::check(tc, true, "run/stop lifecycle executes without exception (EventBus optional)");

    return sqa::test::finalize(tc, "DataCollectorTest");
}

#include "LifeCycleManager/LogData.h"
#include "TestAssert.h"

using namespace sdv::platform::lifecyclemgr;

int main() {
    sqa::test::TestCounter tc;

    LogData logData("AdasFeature");
    sqa::test::check(tc, logData.getFeatureName() == "AdasFeature", "feature name is stored");
    sqa::test::check(tc, logData.getEntryCount() == 0, "initial entry count is zero");

    logData.addEntry("INFO", "started");
    logData.addEntry("WARN", "slow response");
    sqa::test::check(tc, logData.getEntryCount() == 2, "entry count increases after addEntry");

    const auto& entries = logData.getEntries();
    sqa::test::check(tc,
                     entries.size() == 2 &&
                         entries[0].level == "INFO" &&
                         entries[1].message == "slow response",
                     "entries preserve inserted content");

    logData.clear();
    sqa::test::check(tc, logData.getEntryCount() == 0, "clear removes all entries");

    return sqa::test::finalize(tc, "LogDataTest");
}

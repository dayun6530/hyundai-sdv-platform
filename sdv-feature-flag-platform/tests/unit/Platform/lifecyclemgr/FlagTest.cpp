#include "LifeCycleManager/Flag.h"
#include "TestAssert.h"

using namespace sdv::platform::lifecyclemgr;

int main() {
    sqa::test::TestCounter tc;

    Flag flag("NavigationFeature");
    sqa::test::check(tc, flag.getFeatureName() == "NavigationFeature", "feature name is stored");
    sqa::test::check(tc, flag.isEnabled(), "default flag state is enabled");

    flag.disable();
    sqa::test::check(tc, !flag.isEnabled(), "disable turns flag off");

    flag.enable();
    sqa::test::check(tc, flag.isEnabled(), "enable turns flag on");

    return sqa::test::finalize(tc, "FlagTest");
}

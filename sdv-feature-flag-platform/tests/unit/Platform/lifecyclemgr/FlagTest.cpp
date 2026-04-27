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

    sqa::test::check(tc, flag.isSubFeatureEnabled("FCA"), "subfeature defaults on");
    flag.setSubFeatureEnabled("FCA", false);
    sqa::test::check(tc, !flag.isSubFeatureEnabled("FCA"), "subfeature can be turned off");
    flag.setSubFeatureEnabled("FCA", true);
    sqa::test::check(tc, flag.isSubFeatureEnabled("FCA"), "subfeature can be turned back on");

    sqa::test::check(tc, !flag.isShadowMirroringEnabled(), "shadow mirroring defaults off");
    flag.enableShadowMirroring();
    sqa::test::check(tc, flag.isShadowMirroringEnabled(), "shadow mirroring can be enabled");
    flag.disableShadowMirroring();
    sqa::test::check(tc, !flag.isShadowMirroringEnabled(), "shadow mirroring can be disabled");

    sqa::test::check(tc, !flag.isKillSwitchLatched(), "kill switch latch defaults off");
    flag.setKillSwitchLatched(true);
    sqa::test::check(tc, flag.isKillSwitchLatched(), "kill switch latch can be set");

    sqa::test::check(tc, !flag.getAbVariant().has_value(), "A/B variant defaults unset");
    sqa::test::check(tc, !flag.isAbModeA() && !flag.isAbModeB(), "no line mode when variant unset");
    flag.setAbVariant('X');
    sqa::test::check(tc, !flag.getAbVariant().has_value(), "invalid A/B variant is ignored");
    flag.setAbVariant('A');
    sqa::test::check(tc, flag.getAbVariant().has_value() && *flag.getAbVariant() == 'A',
        "A/B variant A is stored");
    sqa::test::check(tc, flag.isAbModeA() && !flag.isAbModeB(), "isAbModeA after set A");
    flag.setAbVariant('B');
    sqa::test::check(tc, flag.isAbModeB() && !flag.isAbModeA(), "isAbModeB after set B");
    flag.clearAbVariant();
    sqa::test::check(tc, !flag.getAbVariant().has_value(), "A/B variant can be cleared");

    return sqa::test::finalize(tc, "FlagTest");
}

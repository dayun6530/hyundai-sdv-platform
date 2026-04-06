#include "Feature/NavigationFeature.h"
#include "FeatureCommunication/EventBus/Event.h"
#include "FeatureCommunication/EventBus/EventId.h"
#include "MockEventBus.h"
#include "TestAssert.h"

#include <stdexcept>

#define main navigation_feature_original_main
#include "src/Feature/src/NavigationFeature_A.cpp"
#include "src/Feature/src/NavigationFeature_B.cpp"
#include "src/Feature/src/NavigationFeature.cpp"
#undef main

using namespace sdv::feature;
using namespace sdv::platform::communications::eventbus;
using sdv::platform::lifecyclemgr::Feature;

namespace {

class ThrowingSendBus final : public sqa::test::MockEventBus {
public:
    bool sendEvent(const Event&) override {
        throw std::runtime_error("send failure");
    }
};

}  // namespace

int main() {
    sqa::test::TestCounter tc;

    ThrowingSendBus throwingBus;
    NavigationFeature throwingFeature(throwingBus);
    bool throws = false;
    try {
        throwingFeature.run();
    } catch (const std::runtime_error&) {
        throws = true;
    }
    sqa::test::check(tc, throws, "NavigationFeature propagates send exceptions");

    sqa::test::MockEventBus successBus;
    Feature::requestStop();
    NavigationFeature feature(successBus);
    feature.run();

    sqa::test::check(tc, successBus.registerCalls == 1, "NavigationFeature registers once");
    sqa::test::check(tc, successBus.unRegisterCalls == 1, "NavigationFeature unregisters on exit");
    sqa::test::check(tc, successBus.sendCalls >= 1, "NavigationFeature sends events");

    bool hasQuit = false;
    for (const auto& event : successBus.sentEvents) {
        if (event.getEventId() == EventId::QuitTheSystem) {
            hasQuit = true;
            break;
        }
    }
    sqa::test::check(tc, hasQuit, "NavigationFeature emits QuitTheSystem");

    return sqa::test::finalize(tc, "NavigationFeatureTest");
}

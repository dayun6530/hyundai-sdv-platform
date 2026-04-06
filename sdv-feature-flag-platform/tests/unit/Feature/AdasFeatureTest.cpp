#include "Feature/AdasFeature.h"
#include "FeatureCommunication/EventBus/Event.h"
#include "FeatureCommunication/EventBus/EventId.h"
#include "MockEventBus.h"
#include "TestAssert.h"

#include <map>
#include <stdexcept>

#define main adas_feature_original_main
#include "src/Feature/src/AdasFeature_A.cpp"
#include "src/Feature/src/AdasFeature_B.cpp"
#include "src/Feature/src/AdasFeature.cpp"
#undef main

using namespace sdv::feature;
using namespace sdv::platform::communications::eventbus;

namespace {

class QuitOnFirstPollBus final : public sqa::test::MockEventBus {
public:
    EventQueue getEventQueue(int64_t componentId) override {
        if (!quitInjected_[componentId]) {
            enqueueFor(componentId, Event(EventId::QuitTheSystem, "test-quit"));
            quitInjected_[componentId] = true;
        }
        return sqa::test::MockEventBus::getEventQueue(componentId);
    }

private:
    std::map<int64_t, bool> quitInjected_;
};

class UnknownThenQuitBus final : public sqa::test::MockEventBus {
public:
    EventQueue getEventQueue(int64_t componentId) override {
        if (!injected_[componentId]) {
            enqueueFor(componentId, Event(static_cast<EventId>(2222), "unknown"));
            enqueueFor(componentId, Event(EventId::QuitTheSystem, "quit"));
            injected_[componentId] = true;
        }
        return sqa::test::MockEventBus::getEventQueue(componentId);
    }

private:
    std::map<int64_t, bool> injected_;
};

class FullFlowBus final : public sqa::test::MockEventBus {
public:
    EventQueue getEventQueue(int64_t componentId) override {
        if (!injected_[componentId]) {
            enqueueFor(componentId, Event(EventId::VehicleLocationUpdate, "loc"));
            enqueueFor(componentId, Event(EventId::RoadInfoUpdate, "road"));
            enqueueFor(componentId, Event(EventId::QuitTheSystem, "quit"));
            injected_[componentId] = true;
        }
        return sqa::test::MockEventBus::getEventQueue(componentId);
    }

private:
    std::map<int64_t, bool> injected_;
};

class ThrowingQueueBus final : public sqa::test::MockEventBus {
public:
    EventQueue getEventQueue(int64_t) override {
        throw std::runtime_error("queue read failed");
    }
};

}  // namespace

int main() {
    sqa::test::TestCounter tc;

    QuitOnFirstPollBus successBus;
    AdasFeature successFeature(successBus);
    successFeature.run();
    sqa::test::check(tc, successBus.registerCalls == 1, "AdasFeature registers once");
    sqa::test::check(tc, successBus.unRegisterCalls == 1, "AdasFeature unregisters on quit");

    FullFlowBus flowBus;
    AdasFeature flowFeature(flowBus);
    bool noThrowFlow = true;
    try {
        flowFeature.run();
    } catch (...) {
        noThrowFlow = false;
    }
    sqa::test::check(tc, noThrowFlow, "AdasFeature handles vehicle/road flow");

    UnknownThenQuitBus unknownBus;
    AdasFeature unknownFeature(unknownBus);
    bool noThrowUnknown = true;
    try {
        unknownFeature.run();
    } catch (...) {
        noThrowUnknown = false;
    }
    sqa::test::check(tc, noThrowUnknown, "AdasFeature ignores unknown event id safely");

    ThrowingQueueBus throwingBus;
    AdasFeature throwingFeature(throwingBus);
    bool throws = false;
    try {
        throwingFeature.run();
    } catch (const std::runtime_error&) {
        throws = true;
    }
    sqa::test::check(tc, throws, "AdasFeature propagates queue exceptions");

    return sqa::test::finalize(tc, "AdasFeatureTest");
}

#include "Feature/EvEnergyFeature.h"
#include "FeatureCommunication/EventBus/Event.h"
#include "FeatureCommunication/EventBus/EventId.h"
#include "MockEventBus.h"
#include "TestAssert.h"

#include <map>
#include <stdexcept>

#define main ev_energy_feature_original_main
#include "src/Feature/src/EvEnergyFeature_A.cpp"
#include "src/Feature/src/EvEnergyFeature_B.cpp"
#include "src/Feature/src/EvEnergyFeature.cpp"
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
            enqueueFor(componentId, Event(static_cast<EventId>(3333), "unknown"));
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
            enqueueFor(componentId, Event(EventId::RoadInfoUpdate, "오르막"));
            enqueueFor(componentId, Event(EventId::RoadInfoUpdate, "내리막"));
            enqueueFor(componentId, Event(EventId::RoadInfoUpdate, "평지"));
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
    EvEnergyFeature successFeature(successBus);
    successFeature.run();
    sqa::test::check(tc, successBus.registerCalls == 1, "EvEnergyFeature registers once");
    sqa::test::check(tc, successBus.unRegisterCalls == 1, "EvEnergyFeature unregisters on quit");

    FullFlowBus flowBus;
    EvEnergyFeature flowFeature(flowBus);
    bool noThrowFlow = true;
    try {
        flowFeature.run();
    } catch (...) {
        noThrowFlow = false;
    }
    sqa::test::check(tc, noThrowFlow, "EvEnergyFeature handles uphill/downhill/flat flow");

    UnknownThenQuitBus unknownBus;
    EvEnergyFeature unknownFeature(unknownBus);
    bool noThrowUnknown = true;
    try {
        unknownFeature.run();
    } catch (...) {
        noThrowUnknown = false;
    }
    sqa::test::check(tc, noThrowUnknown, "EvEnergyFeature ignores unknown event id safely");

    ThrowingQueueBus throwingBus;
    EvEnergyFeature throwingFeature(throwingBus);
    bool throws = false;
    try {
        throwingFeature.run();
    } catch (const std::runtime_error&) {
        throws = true;
    }
    sqa::test::check(tc, throws, "EvEnergyFeature propagates queue exceptions");

    return sqa::test::finalize(tc, "EvEnergyFeatureTest");
}

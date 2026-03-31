#include "LifeCycleManager/WatchDog.h"
#include "FeatureCommunication/EventBus/Event.h"
#include "FeatureCommunication/EventBus/EventId.h"
#include "MockEventBus.h"
#include "TestAssert.h"

#include <map>
#include <stdexcept>

#define main watchdog_original_main
#include "src/Platform/LifeCycleManager/src/WatchDog.cpp"
#undef main

using namespace sdv::platform::lifecyclemgr;
using namespace sdv::platform::communications::eventbus;

namespace {

class QuitOnFirstPollBus final : public sqa::test::MockEventBus {
public:
    EventQueue getEventQueue(int64_t componentId) override {
        if (!quitInjected_[componentId]) {
            enqueueFor(componentId, Event(EventId::QuitTheSystem, "quit"));
            quitInjected_[componentId] = true;
        }
        return sqa::test::MockEventBus::getEventQueue(componentId);
    }

private:
    std::map<int64_t, bool> quitInjected_;
};

class ThrowingQueueBus final : public sqa::test::MockEventBus {
public:
    EventQueue getEventQueue(int64_t) override {
        throw std::runtime_error("queue failure");
    }
};

class UnknownThenQuitBus final : public sqa::test::MockEventBus {
public:
    EventQueue getEventQueue(int64_t componentId) override {
        if (!injected_[componentId]) {
            enqueueFor(componentId, Event(static_cast<EventId>(1234), "unknown"));
            enqueueFor(componentId, Event(EventId::QuitTheSystem, "quit"));
            injected_[componentId] = true;
        }
        return sqa::test::MockEventBus::getEventQueue(componentId);
    }

private:
    std::map<int64_t, bool> injected_;
};

class SilentThenQuitBus final : public sqa::test::MockEventBus {
public:
    EventQueue getEventQueue(int64_t componentId) override {
        pollCount_++;
        if (pollCount_ >= 12 && !quitInjected_[componentId]) {
            enqueueFor(componentId, Event(EventId::QuitTheSystem, "quit"));
            quitInjected_[componentId] = true;
        }
        return sqa::test::MockEventBus::getEventQueue(componentId);
    }

private:
    int pollCount_ = 0;
    std::map<int64_t, bool> quitInjected_;
};

class FlagEventsThenQuitBus final : public sqa::test::MockEventBus {
public:
    EventQueue getEventQueue(int64_t componentId) override {
        if (!injected_[componentId]) {
            enqueueFor(componentId, Event(EventId::FeatureFlagOn, "AdasFeature"));
            enqueueFor(componentId, Event(EventId::FeatureFlagOff, "EvEnergyFeature"));
            enqueueFor(componentId, Event(EventId::KillSwitch, "SomeFeature"));
            enqueueFor(componentId, Event(EventId::QuitTheSystem, "quit"));
            injected_[componentId] = true;
        }
        return sqa::test::MockEventBus::getEventQueue(componentId);
    }

private:
    std::map<int64_t, bool> injected_;
};

}  // namespace

int main() {
    sqa::test::TestCounter tc;

    QuitOnFirstPollBus okBus;
    WatchDog watchDog(okBus);
    watchDog.run();
    sqa::test::check(tc, okBus.registerCalls == 1, "WatchDog registers once");
    sqa::test::check(tc, okBus.unRegisterCalls == 1, "WatchDog unregisters on quit");
    sqa::test::check(tc, okBus.getQueueCalls >= 1, "WatchDog polls queue");

    UnknownThenQuitBus unknownBus;
    WatchDog watchDogUnknown(unknownBus);
    bool noThrowUnknown = true;
    try {
        watchDogUnknown.run();
    } catch (...) {
        noThrowUnknown = false;
    }
    sqa::test::check(tc, noThrowUnknown, "WatchDog handles unknown event without exception");

    SilentThenQuitBus silentBus;
    WatchDog watchDogSilent(silentBus);
    bool noThrowSilent = true;
    try {
        watchDogSilent.run();
    } catch (...) {
        noThrowSilent = false;
    }
    sqa::test::check(tc, noThrowSilent, "WatchDog handles inactivity health-check path");

    ThrowingQueueBus throwingBus;
    WatchDog watchDogThrow(throwingBus);
    bool throws = false;
    try {
        watchDogThrow.run();
    } catch (const std::runtime_error&) {
        throws = true;
    }
    sqa::test::check(tc, throws, "WatchDog propagates queue exception");

    FlagEventsThenQuitBus flagBus;
    WatchDog watchDogFlags(flagBus);
    bool noThrowFlags = true;
    try {
        watchDogFlags.run();
    } catch (...) {
        noThrowFlags = false;
    }
    sqa::test::check(tc, noThrowFlags,
                     "WatchDog handles FLAG_ON/FLAG_OFF/KILL events without exception");

    return sqa::test::finalize(tc, "WatchDogTest");
}

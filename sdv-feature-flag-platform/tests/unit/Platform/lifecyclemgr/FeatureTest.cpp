#include "LifeCycleManager/Feature.h"
#include "FeatureCommunication/EventBus/Event.h"
#include "FeatureCommunication/EventBus/EventId.h"
#include "MockEventBus.h"
#include "TestAssert.h"

#include <chrono>
#include <stdexcept>
#include <thread>

using namespace sdv::platform::lifecyclemgr;
using namespace sdv::platform::communications::eventbus;

namespace {

class OneShotFeature final : public Feature {
public:
    explicit OneShotFeature(EventBusInterface& bus)
        : Feature("OneShotFeature", bus) {}

    int executeCalls = 0;

protected:
    void execute() override { executeCalls++; }
};

class StopAwareFeature final : public Feature {
public:
    explicit StopAwareFeature(EventBusInterface& bus)
        : Feature("StopAwareFeature", bus) {}

protected:
    void execute() override {
        while (!isStopRequested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};

class ThrowingFeature final : public Feature {
public:
    explicit ThrowingFeature(EventBusInterface& bus)
        : Feature("ThrowingFeature", bus) {}

protected:
    void execute() override { throw std::runtime_error("execute failure"); }
};

class RegisterFailBus final : public sqa::test::MockEventBus {
public:
    int64_t registerComponent() override {
        registerCalls++;
        return -1;
    }
};

}  // namespace

int main() {
    sqa::test::TestCounter tc;

    sqa::test::MockEventBus normalBus;
    OneShotFeature oneShot(normalBus);
    oneShot.run();

    sqa::test::check(tc, normalBus.registerCalls == 1, "run calls registerComponent");
    sqa::test::check(tc, oneShot.executeCalls == 1, "run executes derived logic once");
    sqa::test::check(tc, normalBus.unRegisterCalls == 1, "run calls unRegister");
    sqa::test::check(tc,
                     !normalBus.unRegisteredIds.empty() &&
                         normalBus.unRegisteredIds.front() == normalBus.registeredIds.front(),
                     "run unregisters same component id");

    Feature::signalHandler(0);
    StopAwareFeature stopAware(normalBus);
    const auto start = std::chrono::steady_clock::now();
    stopAware.run();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    sqa::test::check(tc, elapsedMs < 200, "signalHandler requests fast stop");

    RegisterFailBus failBus;
    OneShotFeature featureOnFailBus(failBus);
    bool noThrowOnRegisterFail = true;
    try {
        featureOnFailBus.run();
    } catch (...) {
        noThrowOnRegisterFail = false;
    }
    sqa::test::check(tc, noThrowOnRegisterFail, "run handles register failure without exception");
    sqa::test::check(tc,
                     failBus.registerCalls == 1,
                     "register failure path is exercised");

    bool throwsFromExecute = false;
    try {
        ThrowingFeature throwing(normalBus);
        throwing.run();
    } catch (const std::runtime_error&) {
        throwsFromExecute = true;
    }
    sqa::test::check(tc, throwsFromExecute, "run propagates execute exception");

    return sqa::test::finalize(tc, "FeatureTest");
}

#include "FeatureCommunication/EventBus/EventBusInterface.h"
#include "FeatureCommunication/EventBus/EventId.h"
#include "MockEventBus.h"
#include "TestAssert.h"

#include <type_traits>

using namespace sdv::platform::communications::eventbus;

int main() {
    sqa::test::TestCounter tc;

    sqa::test::check(tc,
                     std::is_abstract<EventBusInterface>::value,
                     "EventBusInterface is abstract");

    sqa::test::MockEventBus mock;
    EventBusInterface* iface = &mock;
    const int64_t id = iface->registerComponent();
    sqa::test::check(tc, id > 0, "registerComponent returns valid id");

    const bool sendResult = iface->sendEvent(Event(EventId::VehicleLocationUpdate, "msg"));
    sqa::test::check(tc, sendResult && mock.sendCalls == 1, "sendEvent returns success and dispatches");

    EventQueue queue = iface->getEventQueue(id);
    sqa::test::check(tc, queue.getSize() == 0, "empty queue retrieval succeeds");

    // Failure-style case: unknown component id still returns a valid empty queue object.
    EventQueue unknown = iface->getEventQueue(99999);
    sqa::test::check(tc,
                     unknown.getSize() == 0 && unknown.getId() == 99999,
                     "unknown component id returns empty queue");

    iface->unRegister(id);
    sqa::test::check(tc, mock.unRegisterCalls == 1, "unRegister is called once");

    return sqa::test::finalize(tc, "EventBusInterfaceTest");
}

#include "FeatureCommunication/EventBus/EventQueue.h"
#include "FeatureCommunication/EventBus/EventId.h"
#include "TestAssert.h"

#include <stdexcept>

using namespace sdv::platform::communications::eventbus;

int main() {
    sqa::test::TestCounter tc;

    EventQueue q(42);
    sqa::test::check(tc, q.getId() == 42, "EventQueue keeps ctor component id");
    sqa::test::check(tc, q.getSize() == 0, "EventQueue starts empty");

    const bool addedA = q.addEvent(Event(EventId::VehicleLocationUpdate, "a"));
    const bool addedB = q.addEvent(Event(EventId::RoadInfoUpdate, "b"));
    sqa::test::check(tc, addedA && addedB, "addEvent returns true under capacity");
    sqa::test::check(tc, q.getSize() == 2, "size after add");

    Event first = q.getEvent();
    sqa::test::check(tc,
                     first.getEventId() == EventId::VehicleLocationUpdate &&
                         first.getMessage() == "a",
                     "FIFO order is preserved");

    EventQueue copied = q.getCopy();
    sqa::test::check(tc, copied.getId() == q.getId(), "copy keeps component id");
    sqa::test::check(tc, copied.getSize() == q.getSize(), "copy keeps queue size");

    q.clearEventQueue();
    sqa::test::check(tc, q.getSize() == 0, "clearEventQueue clears all events");

    bool threw = false;
    try {
        q.getEvent();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    sqa::test::check(tc, threw, "getEvent throws on empty queue");

    EventQueue bounded(77);
    for (int i = 0; i < EventQueue::maxQueueSize; ++i) {
        bounded.addEvent(Event(EventId::VehicleLocationUpdate, "x"));
    }
    const bool overLimitAdd = bounded.addEvent(Event(EventId::RoadInfoUpdate, "overflow"));
    sqa::test::check(tc, !overLimitAdd, "addEvent returns false when queue is full");

    return sqa::test::finalize(tc, "EventQueueTest");
}

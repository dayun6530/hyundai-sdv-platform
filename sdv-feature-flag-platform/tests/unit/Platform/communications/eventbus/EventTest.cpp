#include "FeatureCommunication/EventBus/Event.h"
#include "FeatureCommunication/EventBus/EventId.h"
#include "TestAssert.h"

using namespace sdv::platform::communications::eventbus;

int main() {
    sqa::test::TestCounter tc;

    Event withMessage(EventId::VehicleLocationUpdate, "location");
    sqa::test::check(tc,
                     withMessage.getEventId() == EventId::VehicleLocationUpdate,
                     "Event id is stored");
    sqa::test::check(tc,
                     withMessage.getMessage() == "location",
                     "Event message is stored");

    Event emptyMessage(EventId::QuitTheSystem);
    sqa::test::check(tc,
                     emptyMessage.getEventId() == EventId::QuitTheSystem,
                     "Event single-arg ctor stores id");
    sqa::test::check(tc,
                     emptyMessage.getMessage().empty(),
                     "Event single-arg ctor sets empty message");

    // Failure-style input: empty string is accepted as a valid payload.
    Event blank(EventId::RoadInfoUpdate, "");
    sqa::test::check(tc,
                     blank.getMessage().empty(),
                     "Event accepts blank payload without exception");

    return sqa::test::finalize(tc, "EventTest");
}

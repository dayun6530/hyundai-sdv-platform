#include "FeatureCommunication/EventBus/Event.h"

namespace sdv::platform::communications::eventbus {

Event::Event(EventId id, const std::string& message)
    : eventId_(id), message_(message) {}

Event::Event(EventId id)
    : eventId_(id) {}

EventId Event::getEventId() const {
    return eventId_;
}

const std::string& Event::getMessage() const {
    return message_;
}

}  // namespace sdv::platform::communications::eventbus

#pragma once

#include "FeatureCommunication/EventBus/EventId.h"

#include <string>

namespace sdv::platform::communications::eventbus {

class Event {
public:
    Event(EventId id, const std::string& message);
    explicit Event(EventId id);

    EventId getEventId() const;
    const std::string& getMessage() const;

private:
    EventId eventId_;
    std::string message_;
};

}  // namespace sdv::platform::communications::eventbus

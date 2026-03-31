#pragma once

#include "FeatureCommunication/EventBus/Event.h"

#include <vector>
#include <cstdint>

namespace sdv::platform::communications::eventbus {

class EventQueue {
public:
    static constexpr int maxQueueSize = 1000;

    explicit EventQueue(int64_t componentId);

    int64_t getId() const;
    int getSize() const;

    bool addEvent(const Event& newEvent);
    Event getEvent();
    void clearEventQueue();
    EventQueue getCopy() const;

private:
    std::vector<Event> eventList_;
    int64_t componentId_;
};

}  // namespace sdv::platform::communications::eventbus

#include "FeatureCommunication/EventBus/EventQueue.h"

#include <stdexcept>

namespace sdv::platform::communications::eventbus {

EventQueue::EventQueue(int64_t componentId)
    : componentId_(componentId) {}

int64_t EventQueue::getId() const {
    return componentId_;
}

int EventQueue::getSize() const {
    return static_cast<int>(eventList_.size());
}

bool EventQueue::addEvent(const Event& newEvent) {
    if (static_cast<int>(eventList_.size()) >= maxQueueSize) {
        return false;
    }
    eventList_.push_back(newEvent);
    return true;
}

Event EventQueue::getEvent() {
    if (eventList_.empty()) {
        throw std::runtime_error("EventQueue is empty");
    }
    Event event = eventList_.front();
    eventList_.erase(eventList_.begin());
    return event;
}

void EventQueue::clearEventQueue() {
    eventList_.clear();
}

EventQueue EventQueue::getCopy() const {
    EventQueue copy(componentId_);
    copy.eventList_ = eventList_;
    return copy;
}

}  // namespace sdv::platform::communications::eventbus

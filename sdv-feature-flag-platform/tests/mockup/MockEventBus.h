#pragma once

#include "FeatureCommunication/EventBus/EventBusInterface.h"

#include <cstdint>
#include <map>
#include <vector>

namespace sqa::test {

class MockEventBus : public sdv::platform::communications::eventbus::EventBusInterface {
public:
    using Event = sdv::platform::communications::eventbus::Event;
    using EventQueue = sdv::platform::communications::eventbus::EventQueue;

    int64_t registerComponent() override {
        const int64_t id = nextId_++;
        registerCalls++;
        registeredIds.push_back(id);
        queues_.emplace(id, EventQueue(id));
        return id;
    }

    void unRegister(int64_t componentId) override {
        unRegisterCalls++;
        unRegisteredIds.push_back(componentId);
        queues_.erase(componentId);
    }

    bool sendEvent(const Event& event) override {
        sendCalls++;
        sentEvents.push_back(event);
        return true;
    }

    EventQueue getEventQueue(int64_t componentId) override {
        getQueueCalls++;
        auto it = queues_.find(componentId);
        if (it == queues_.end()) {
            return EventQueue(componentId);
        }
        EventQueue copy = it->second.getCopy();
        it->second.clearEventQueue();
        return copy;
    }

    void enqueueFor(int64_t componentId, const Event& event) {
        auto it = queues_.find(componentId);
        if (it == queues_.end()) {
            it = queues_.emplace(componentId, EventQueue(componentId)).first;
        }
        it->second.addEvent(event);
    }

    int registerCalls = 0;
    int unRegisterCalls = 0;
    int sendCalls = 0;
    int getQueueCalls = 0;
    std::vector<int64_t> registeredIds;
    std::vector<int64_t> unRegisteredIds;
    std::vector<Event> sentEvents;

private:
    int64_t nextId_ = 1000;
    std::map<int64_t, EventQueue> queues_;
};

}  // namespace sqa::test

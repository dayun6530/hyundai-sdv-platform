#pragma once

#include "FeatureCommunication/EventBus/Event.h"
#include "FeatureCommunication/EventBus/EventQueue.h"

#include <cstdint>

namespace sdv::platform::communications::eventbus {

class EventBusInterface {
public:
    virtual ~EventBusInterface() = default;

    virtual int64_t registerComponent() = 0;
    virtual void unRegister(int64_t componentId) = 0;
    virtual bool sendEvent(const Event& event) = 0;
    virtual EventQueue getEventQueue(int64_t componentId) = 0;
};

}  // namespace sdv::platform::communications::eventbus

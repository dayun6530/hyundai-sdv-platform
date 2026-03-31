#pragma once

#include "FeatureCommunication/EventBus/EventBusInterface.h"

#include <grpcpp/grpcpp.h>
#include "eventbus.grpc.pb.h"

#include <memory>
#include <string>

namespace sdv::platform::communications::eventbus {

class EventBusProxy : public EventBusInterface {
public:
    explicit EventBusProxy(const std::string& address);

    int64_t registerComponent() override;
    void unRegister(int64_t componentId) override;
    bool sendEvent(const Event& event) override;
    EventQueue getEventQueue(int64_t componentId) override;

private:
    std::unique_ptr<sdv::platform::EventBus::Stub> stub_;
};

}  // namespace sdv::platform::communications::eventbus

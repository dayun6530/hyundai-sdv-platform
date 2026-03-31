#include "FeatureCommunication/EventBus/EventBusProxy.h"

#include <iostream>

namespace sdv::platform::communications::eventbus {

EventBusProxy::EventBusProxy(const std::string& address)
    : stub_(sdv::platform::EventBus::NewStub(
          grpc::CreateChannel(address, grpc::InsecureChannelCredentials()))) {}

int64_t EventBusProxy::registerComponent() {
    RegisterRequest request;
    RegisterResponse response;
    grpc::ClientContext context;

    grpc::Status status = stub_->registerComponent(&context, request, &response);
    if (!status.ok()) {
        std::cerr << "registerComponent failed: " << status.error_message() << std::endl;
        return -1;
    }
    return response.component_id();
}

void EventBusProxy::unRegister(int64_t componentId) {
    UnRegisterRequest request;
    request.set_component_id(componentId);
    UnRegisterResponse response;
    grpc::ClientContext context;

    grpc::Status status = stub_->unRegister(&context, request, &response);
    if (!status.ok()) {
        std::cerr << "unRegister failed: " << status.error_message() << std::endl;
    }
}

bool EventBusProxy::sendEvent(const Event& event) {
    SendEventRequest request;
    request.set_event_id(static_cast<int32_t>(event.getEventId()));
    request.set_message(event.getMessage());
    SendEventResponse response;
    grpc::ClientContext context;

    grpc::Status status = stub_->sendEvent(&context, request, &response);
    if (!status.ok()) {
        std::cerr << "sendEvent failed: " << status.error_message() << std::endl;
        return false;
    }
    return true;
}

EventQueue EventBusProxy::getEventQueue(int64_t componentId) {
    GetEventQueueRequest request;
    request.set_component_id(componentId);
    GetEventQueueResponse response;
    grpc::ClientContext context;

    grpc::Status status = stub_->getEventQueue(&context, request, &response);
    if (!status.ok()) {
        std::cerr << "getEventQueue failed: " << status.error_message() << std::endl;
        return EventQueue(componentId);
    }

    EventQueue queue(response.component_id());
    for (const auto& msg : response.events()) {
        EventId eventId = static_cast<EventId>(msg.event_id());
        Event event(eventId, msg.message());
        queue.addEvent(event);
    }
    return queue;
}

}  // namespace sdv::platform::communications::eventbus

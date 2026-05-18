#include "FeatureCommunication/EventBus/EventBusProxy.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace sdv::platform::communications::eventbus {

EventBusProxy::EventBusProxy(const std::string& address)
    : stub_(sdv::platform::EventBus::NewStub(
          grpc::CreateChannel(address, grpc::InsecureChannelCredentials()))) {}

EventBusProxy::~EventBusProxy() {
    std::vector<int64_t> ids;
    {
        std::lock_guard<std::mutex> lock(streamMutex_);
        for (const auto& [id, _] : streamReaders_) {
            ids.push_back(id);
        }
    }
    for (const int64_t id : ids) {
        stopStreamReader(id);
    }
}

void EventBusProxy::appendBufferedEvent(int64_t componentId, const Event& event) {
    std::lock_guard<std::mutex> lock(streamMutex_);
    const auto it = streamReaders_.find(componentId);
    if (it == streamReaders_.end() || !it->second) {
        return;
    }
    std::lock_guard<std::mutex> bufLock(it->second->bufferMu);
    it->second->buffer.push_back(event);
}

void EventBusProxy::startStreamReader(int64_t componentId) {
    std::lock_guard<std::mutex> lock(streamMutex_);
    if (streamReaders_.count(componentId) != 0) {
        return;
    }

    auto state = std::make_unique<ComponentStreamState>();
    state->running.store(true);
    state->context = std::make_unique<grpc::ClientContext>();

    SubscribeEventsRequest request;
    request.set_component_id(componentId);

    std::unique_ptr<grpc::ClientReader<EventMessage>> reader(
        stub_->SubscribeEvents(state->context.get(), request));

    ComponentStreamState* statePtr = state.get();
    state->reader = std::thread([this, componentId, statePtr, reader = std::move(reader)]() mutable {
        EventMessage msg;
        while (statePtr->running.load() && reader->Read(&msg)) {
            const Event event(static_cast<EventId>(msg.event_id()), msg.message());
            appendBufferedEvent(componentId, event);
        }
        statePtr->running.store(false);
    });

    streamReaders_[componentId] = std::move(state);
}

void EventBusProxy::stopStreamReader(int64_t componentId) {
    std::unique_ptr<ComponentStreamState> state;
    {
        std::lock_guard<std::mutex> lock(streamMutex_);
        const auto it = streamReaders_.find(componentId);
        if (it == streamReaders_.end()) {
            return;
        }
        state = std::move(it->second);
        streamReaders_.erase(it);
    }

    state->running.store(false);
    if (state->context) {
        state->context->TryCancel();
    }
    if (state->reader.joinable()) {
        state->reader.join();
    }
}

int64_t EventBusProxy::registerComponent() {
    RegisterRequest request;
    RegisterResponse response;
    grpc::ClientContext context;

    grpc::Status status = stub_->registerComponent(&context, request, &response);
    if (!status.ok()) {
        std::cerr << "registerComponent failed: " << status.error_message() << std::endl;
        return -1;
    }
    const int64_t id = response.component_id();
    startStreamReader(id);
    return id;
}

void EventBusProxy::unRegister(int64_t componentId) {
    stopStreamReader(componentId);

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
    EventQueue drained(componentId);
    std::lock_guard<std::mutex> lock(streamMutex_);
    const auto it = streamReaders_.find(componentId);
    if (it == streamReaders_.end() || !it->second) {
        return drained;
    }

    std::deque<Event> pending;
    {
        std::lock_guard<std::mutex> bufLock(it->second->bufferMu);
        pending.swap(it->second->buffer);
    }
    for (const Event& event : pending) {
        drained.addEvent(event);
    }
    return drained;
}

void EventBusProxy::consumeEventStream(int64_t componentId,
                                       const std::function<bool()>& shouldContinue,
                                       const std::function<void(const Event&)>& onEvent) {
    stopStreamReader(componentId);

    while (shouldContinue()) {
        grpc::ClientContext context;
        SubscribeEventsRequest request;
        request.set_component_id(componentId);

        std::unique_ptr<grpc::ClientReader<EventMessage>> reader(
            stub_->SubscribeEvents(&context, request));
        if (!reader) {
            std::cerr << "SubscribeEvents failed to start for component " << componentId << std::endl;
            return;
        }

        std::thread cancelWatcher([&context, &shouldContinue]() {
            while (shouldContinue()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            context.TryCancel();
        });

        EventMessage msg;
        while (reader->Read(&msg)) {
            if (!shouldContinue()) {
                break;
            }
            const Event event(static_cast<EventId>(msg.event_id()), msg.message());
            onEvent(event);
        }

        context.TryCancel();
        if (cancelWatcher.joinable()) {
            cancelWatcher.join();
        }

        if (!shouldContinue()) {
            return;
        }

        std::cerr << "[EventBusProxy] SubscribeEvents stream ended for component " << componentId
                  << "; reconnecting..." << std::endl;
    }
}

}  // namespace sdv::platform::communications::eventbus

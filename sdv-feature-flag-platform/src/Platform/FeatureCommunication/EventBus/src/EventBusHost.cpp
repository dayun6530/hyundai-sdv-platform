#include "FeatureCommunication/EventBus/EventBusHost.h"

#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <signal.h>

namespace sdv::platform::communications::eventbus {

namespace {

EventMessage toProtoMessage(const Event& event) {
    EventMessage msg;
    msg.set_event_id(static_cast<int32_t>(event.getEventId()));
    msg.set_message(event.getMessage());
    return msg;
}

}  // namespace

EventBusHost::EventBusHost()
    : nextComponentId_(1) {}

bool EventBusHost::pushEventToStream(int64_t componentId, const Event& event) {
    std::shared_ptr<StreamSubscriber> sub;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = subscribers_.find(componentId);
        if (it == subscribers_.end() || !it->second || !it->second->active.load()) {
            return false;
        }
        sub = it->second;
    }

    const EventMessage msg = toProtoMessage(event);
    std::lock_guard<std::mutex> writeLock(sub->writeMu);
    if (!sub->active.load() || sub->writer == nullptr) {
        return false;
    }
    if (!sub->writer->Write(msg)) {
        sub->active.store(false);
        return false;
    }
    return true;
}

void EventBusHost::flushQueueToStream(EventQueue& queue,
                                      const std::shared_ptr<StreamSubscriber>& sub) {
    while (queue.getSize() > 0) {
        const Event event = queue.getEvent();
        const EventMessage msg = toProtoMessage(event);

        std::lock_guard<std::mutex> writeLock(sub->writeMu);
        if (!sub->active.load() || sub->writer == nullptr) {
            return;
        }
        if (!sub->writer->Write(msg)) {
            sub->active.store(false);
            return;
        }
    }
}

grpc::Status EventBusHost::registerComponent(grpc::ServerContext* /*context*/,
                                              const RegisterRequest* /*request*/,
                                              RegisterResponse* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t newId = nextComponentId_.fetch_add(1);
    EventQueue newEventQueue(newId);
    eventQueueList_.push_back(newEventQueue);
    response->set_component_id(newId);
    std::cout << "Component (ID:" << newId << ") is registered..." << std::endl;
    return grpc::Status::OK;
}

grpc::Status EventBusHost::unRegister(grpc::ServerContext* /*context*/,
                                      const UnRegisterRequest* request,
                                      UnRegisterResponse* /*response*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t id = request->component_id();
    subscribers_.erase(id);
    for (auto it = eventQueueList_.begin(); it != eventQueueList_.end(); ++it) {
        if (it->getId() == id) {
            eventQueueList_.erase(it);
            std::cout << "Component (ID:" << id << ") is unregistered..." << std::endl;
            break;
        }
    }
    return grpc::Status::OK;
}

grpc::Status EventBusHost::sendEvent(grpc::ServerContext* /*context*/,
                                     const SendEventRequest* request,
                                     SendEventResponse* /*response*/) {
    EventId eventId = static_cast<EventId>(request->event_id());
    Event sentEvent(eventId, request->message());

    std::vector<int64_t> targetIds;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int totalQueues = static_cast<int>(eventQueueList_.size());
        int droppedCount = 0;
        for (auto& eventQueue : eventQueueList_) {
            if (!eventQueue.addEvent(sentEvent)) {
                std::cerr << "WARNING: Queue (ID:" << eventQueue.getId()
                          << ") is full, event dropped" << std::endl;
                droppedCount++;
            } else {
                targetIds.push_back(eventQueue.getId());
            }
        }
        std::cout << "Event Information(ID: " << request->event_id()
                  << ", Message: " << request->message() << ")" << std::endl;

        if (totalQueues > 0 && droppedCount == totalQueues) {
            return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                                "all queues full, event dropped");
        }
    }

    for (const int64_t componentId : targetIds) {
        pushEventToStream(componentId, sentEvent);
    }
    return grpc::Status::OK;
}

grpc::Status EventBusHost::SubscribeEvents(grpc::ServerContext* context,
                                           const SubscribeEventsRequest* request,
                                           grpc::ServerWriter<EventMessage>* writer) {
    const int64_t id = request->component_id();
    auto sub = std::make_shared<StreamSubscriber>();
    sub->writer = writer;
    sub->active.store(true);

  {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_[id] = sub;
        for (auto& queue : eventQueueList_) {
            if (queue.getId() == id) {
                flushQueueToStream(queue, sub);
                break;
            }
        }
    }

    while (!context->IsCancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = subscribers_.find(id);
        if (it != subscribers_.end() && it->second == sub) {
            subscribers_.erase(it);
        }
    }
    sub->active.store(false);
    return grpc::Status::OK;
}

void EventBusHost::runServer(const std::string& address) {
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(this);

    server_ = builder.BuildAndStart();
    if (!server_) {
        std::cerr << "EventBusHost: BuildAndStart failed (address=" << address
                  << "). Is the port already in use?" << std::endl;
        std::_Exit(1);
    }
    std::cout << "Event Bus is running now on " << address << "..." << std::endl;
    server_->Wait();
}

void EventBusHost::shutdownServer() {
    if (server_) {
        server_->Shutdown();
    }
}

}  // namespace sdv::platform::communications::eventbus

static std::atomic<bool> shutdownRequested(false);

void hostSignalHandler(int) {
    shutdownRequested = true;
}

int main(int argc, char* argv[]) {
    std::string address = "0.0.0.0:50051";
    if (argc > 1) address = argv[1];

    signal(SIGTERM, hostSignalHandler);
    signal(SIGINT, hostSignalHandler);

    sdv::platform::communications::eventbus::EventBusHost host;

    std::thread serverThread([&host, &address]() {
        host.runServer(address);
    });

    while (!shutdownRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    host.shutdownServer();
    serverThread.join();

    return 0;
}

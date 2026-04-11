#include "FeatureCommunication/EventBus/EventBusHost.h"

#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <signal.h>

namespace sdv::platform::communications::eventbus {

EventBusHost::EventBusHost()
    : nextComponentId_(1) {}

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
    std::lock_guard<std::mutex> lock(mutex_);
    EventId eventId = static_cast<EventId>(request->event_id());
    Event sentEvent(eventId, request->message());

    int totalQueues = static_cast<int>(eventQueueList_.size());
    int droppedCount = 0;
    for (auto& eventQueue : eventQueueList_) {
        if (!eventQueue.addEvent(sentEvent)) {
            std::cerr << "WARNING: Queue (ID:" << eventQueue.getId()
                      << ") is full, event dropped" << std::endl;
            droppedCount++;
        }
    }
    std::cout << "Event Information(ID: " << request->event_id()
              << ", Message: " << request->message() << ")" << std::endl;

    if (totalQueues > 0 && droppedCount == totalQueues) {
        return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                            "all queues full, event dropped");
    }
    return grpc::Status::OK;
}

grpc::Status EventBusHost::getEventQueue(grpc::ServerContext* /*context*/,
                                         const GetEventQueueRequest* request,
                                         GetEventQueueResponse* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t id = request->component_id();

    for (auto& originalQueue : eventQueueList_) {
        if (originalQueue.getId() == id) {
            EventQueue copiedQueue = originalQueue.getCopy();
            response->set_component_id(copiedQueue.getId());

            while (copiedQueue.getSize() > 0) {
                Event event = copiedQueue.getEvent();
                auto* msg = response->add_events();
                msg->set_event_id(static_cast<int32_t>(event.getEventId()));
                msg->set_message(event.getMessage());
            }
            originalQueue.clearEventQueue();
            break;
        }
    }
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

#include "FeatureCommunication/EventBus/EventBusHost.h"
#include "SdvSpacedConsole.h"

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

std::string stripMirrorPrefixForLog(const std::string& msg) {
    if (msg.size() < 8 || msg.compare(0, 7, "MIRROR|") != 0) {
        return msg;
    }
    const auto secondBar = msg.find('|', 7);
    if (secondBar == std::string::npos || secondBar + 1 >= msg.size()) {
        return msg;
    }
    return msg.substr(secondBar + 1);
}

std::string splitAtFirstPipeForLog(const std::string& msg) {
    const auto bar = msg.find('|');
    if (bar == std::string::npos || bar + 1 >= msg.size()) {
        return msg;
    }
    return msg.substr(0, bar) + '\n' + msg.substr(bar + 1);
}

std::string splitAtLastPipeForLog(const std::string& msg) {
    const auto bar = msg.rfind('|');
    if (bar == std::string::npos || bar + 1 >= msg.size()) {
        return msg;
    }
    return msg.substr(0, bar) + '\n' + msg.substr(bar + 1);
}

std::string splitParentheticalToNewLineForLog(const std::string& msg) {
    const auto open = msg.find(" (");
    if (open == std::string::npos) {
        return msg;
    }
    return msg.substr(0, open) + '\n' + msg.substr(open + 1);
}

std::string formatShadowMirrorBodyForLog(const std::string& body) {
    const auto colon = body.find(':');
    if (colon == std::string::npos || colon + 1 >= body.size()) {
        return splitParentheticalToNewLineForLog(body);
    }

    const std::string featurePrefix = body.substr(0, colon + 1);
    const std::string payload = body.substr(colon + 1);

    auto splitLocationFromPayload = [&](char pipeMode) -> std::string {
        const auto bar = (pipeMode == 'l') ? payload.rfind('|') : payload.find('|');
        if (bar == std::string::npos || bar + 1 >= payload.size()) {
            return body;
        }
        const std::string head = featurePrefix + payload.substr(0, bar);
        std::string location = payload.substr(bar + 1);
        location = splitParentheticalToNewLineForLog(location);
        return head + '\n' + location;
    };

    if (payload.rfind("ADAS_DECISION", 0) == 0 ||
        payload.find("ADAS_DECISION") != std::string::npos) {
        return splitLocationFromPayload('f');  // first '|' = location start
    }
    if (payload.rfind("ENERGY_STATE", 0) == 0 ||
        payload.find("ENERGY_STATE") != std::string::npos) {
        return splitLocationFromPayload('l');  // last '|' = location start (after SHADOW_*)
    }

    return splitParentheticalToNewLineForLog(body);
}

std::string stripJsonStringFieldForLog(std::string msg, const char* key) {
    const std::string needle = std::string("\"") + key + "\":\"";
    for (;;) {
        const auto pos = msg.find(needle);
        if (pos == std::string::npos) {
            break;
        }
        const auto valueEnd = msg.find('"', pos + needle.size());
        if (valueEnd == std::string::npos) {
            break;
        }
        size_t eraseEnd = valueEnd + 1;
        if (eraseEnd < msg.size() && msg[eraseEnd] == ',') {
            ++eraseEnd;
        }
        msg.erase(pos, eraseEnd - pos);
    }
    return msg;
}

std::string stripTelemetryMetaFieldsForLog(const std::string& msg) {
    std::string out = stripJsonStringFieldForLog(msg, "timestamp");
    out = stripJsonStringFieldForLog(out, "car_id");
    if (out.size() > 1 && out[0] == '{' && out[1] == ',') {
        out.erase(1, 1);
    }
    return out;
}

std::string formatEventInformationMessage(int32_t eventId, const std::string& raw) {
    switch (eventId) {
    case static_cast<int32_t>(EventId::ShadowMirrorData): {
        std::string body = stripMirrorPrefixForLog(raw);
        return formatShadowMirrorBodyForLog(body);
    }
    case static_cast<int32_t>(EventId::TelemetryData):
        return stripTelemetryMetaFieldsForLog(raw);
    case static_cast<int32_t>(EventId::AdasDecisionUpdate):
        return splitAtFirstPipeForLog(raw);
    case static_cast<int32_t>(EventId::EnergyStateUpdate):
        return splitAtLastPipeForLog(raw);
    default:
        return raw;
    }
}

bool shouldLogEventInformation(int32_t eventId) {
    switch (eventId) {
    case static_cast<int32_t>(EventId::VehicleLocationUpdate):
    case static_cast<int32_t>(EventId::RoadInfoUpdate):
    case static_cast<int32_t>(EventId::NavigationGuidanceUpdate):
    case static_cast<int32_t>(EventId::ShadowModeOn):
        return false;
    default:
        return true;
    }
}

}  // namespace

EventBusHost::EventBusHost()
    : nextComponentId_(1) {}

bool EventBusHost::writeEventToSubscriber(const std::shared_ptr<StreamSubscriber>& sub,
                                          const Event& event) {
    if (!sub || !sub->active.load()) {
        return false;
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
    return writeEventToSubscriber(sub, event);
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
    SDV_SPC_COUT << "Component (ID:" << newId << ") is registered..." << std::endl;
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
            SDV_SPC_COUT << "Component (ID:" << id << ") is unregistered..." << std::endl;
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

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const int totalQueues = static_cast<int>(eventQueueList_.size());
        int droppedCount = 0;
        for (auto& eventQueue : eventQueueList_) {
            const int64_t componentId = eventQueue.getId();
            const auto subIt = subscribers_.find(componentId);
            if (subIt != subscribers_.end() && subIt->second && subIt->second->active.load()) {
                if (writeEventToSubscriber(subIt->second, sentEvent)) {
                    continue;
                }
            }
            if (!eventQueue.addEvent(sentEvent)) {
                SDV_SPC_CERR << "WARNING: Queue (ID:" << componentId
                          << ") is full, event dropped" << std::endl;
                droppedCount++;
            }
        }
        if (shouldLogEventInformation(request->event_id())) {
            SDV_SPC_COUT << "Event Information(ID: " << request->event_id()
                      << ", Message: "
                      << formatEventInformationMessage(request->event_id(), request->message())
                      << ")" << std::endl;
        }

        if (totalQueues > 0 && droppedCount == totalQueues) {
            return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                                "all queues full, event dropped");
        }
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
        SDV_SPC_CERR << "EventBusHost: BuildAndStart failed (address=" << address
                  << "). Is the port already in use?" << std::endl;
        std::_Exit(1);
    }
    SDV_SPC_COUT << "Event Bus is running now on " << address << "..." << std::endl;
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

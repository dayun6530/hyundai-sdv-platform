#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"
#include "FeatureCommunication/EventBus/EventQueue.h"
#include "TestAssert.h"

#include <grpcpp/grpcpp.h>
#include "eventbus.grpc.pb.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace sdv::platform::communications::eventbus;

class FakeEventBusService final : public sdv::platform::EventBus::Service {
public:
    grpc::Status registerComponent(grpc::ServerContext*,
                                   const sdv::platform::RegisterRequest*,
                                   sdv::platform::RegisterResponse* response) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const int64_t id = nextId_++;
        queues_[id] = std::vector<EventMessageData>();
        response->set_component_id(id);
        return grpc::Status::OK;
    }

    grpc::Status unRegister(grpc::ServerContext*,
                            const sdv::platform::UnRegisterRequest* request,
                            sdv::platform::UnRegisterResponse*) override {
        std::lock_guard<std::mutex> lock(mutex_);
        queues_.erase(request->component_id());
        return grpc::Status::OK;
    }

    grpc::Status sendEvent(grpc::ServerContext*,
                           const sdv::platform::SendEventRequest* request,
                           sdv::platform::SendEventResponse*) override {
        std::lock_guard<std::mutex> lock(mutex_);
        EventMessageData msg;
        msg.eventId = request->event_id();
        msg.message = request->message();
        for (auto& [id, queue] : queues_) {
            (void)id;
            queue.push_back(msg);
        }
        return grpc::Status::OK;
    }

    grpc::Status getEventQueue(grpc::ServerContext*,
                               const sdv::platform::GetEventQueueRequest* request,
                               sdv::platform::GetEventQueueResponse* response) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const int64_t id = request->component_id();
        response->set_component_id(id);
        auto it = queues_.find(id);
        if (it == queues_.end()) {
            return grpc::Status::OK;
        }

        for (const auto& entry : it->second) {
            auto* event = response->add_events();
            event->set_event_id(entry.eventId);
            event->set_message(entry.message);
        }
        it->second.clear();
        return grpc::Status::OK;
    }

private:
    struct EventMessageData {
        int32_t eventId;
        std::string message;
    };

    std::mutex mutex_;
    std::atomic<int64_t> nextId_{1};
    std::map<int64_t, std::vector<EventMessageData>> queues_;
};

int main() {
    sqa::test::TestCounter tc;

    FakeEventBusService service;
    grpc::ServerBuilder builder;
    int selectedPort = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selectedPort);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();

    std::thread serverThread([&server]() { server->Wait(); });

    const std::string address = "127.0.0.1:" + std::to_string(selectedPort);
    EventBusProxy proxy1(address);
    EventBusProxy proxy2(address);

    const int64_t id1 = proxy1.registerComponent();
    const int64_t id2 = proxy2.registerComponent();
    sqa::test::check(tc, id1 > 0 && id2 > 0 && id1 != id2, "registerComponent success path");

    proxy1.sendEvent(Event(EventId::VehicleLocationUpdate, "msg-a"));

    EventQueue q1 = proxy1.getEventQueue(id1);
    EventQueue q2 = proxy2.getEventQueue(id2);
    sqa::test::check(tc, q1.getSize() == 1 && q2.getSize() == 1, "sendEvent broadcast success");

    EventQueue afterRead = proxy1.getEventQueue(id1);
    sqa::test::check(tc, afterRead.getSize() == 0, "getEventQueue clears queue on server");

    proxy1.unRegister(id1);
    proxy2.sendEvent(Event(EventId::RoadInfoUpdate, "msg-b"));
    EventQueue q1AfterUnreg = proxy1.getEventQueue(id1);
    sqa::test::check(tc, q1AfterUnreg.getSize() == 0, "unregistered component receives no events");

    // Failure path: unreachable server should return failure values instead of throwing.
    EventBusProxy badProxy("127.0.0.1:1");
    const int64_t badId = badProxy.registerComponent();
    sqa::test::check(tc, badId == -1, "registerComponent returns -1 on RPC failure");

    EventQueue badQueue = badProxy.getEventQueue(12345);
    sqa::test::check(tc, badQueue.getSize() == 0, "getEventQueue returns empty queue on RPC failure");

    bool noThrow = true;
    try {
        badProxy.sendEvent(Event(EventId::QuitTheSystem, "x"));
        badProxy.unRegister(12345);
    } catch (...) {
        noThrow = false;
    }
    sqa::test::check(tc, noThrow, "void RPC methods do not throw on RPC failure");

    server->Shutdown();
    serverThread.join();

    return sqa::test::finalize(tc, "EventBusProxyTest");
}

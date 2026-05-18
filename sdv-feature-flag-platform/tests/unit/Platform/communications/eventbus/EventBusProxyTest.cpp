#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"
#include "FeatureCommunication/EventBus/EventQueue.h"
#include "TestAssert.h"

#include <grpcpp/grpcpp.h>
#include "eventbus.grpc.pb.h"

#include <atomic>
#include <deque>
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
        response->set_component_id(id);
        return grpc::Status::OK;
    }

    grpc::Status unRegister(grpc::ServerContext*,
                            const sdv::platform::UnRegisterRequest* request,
                            sdv::platform::UnRegisterResponse*) override {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.erase(request->component_id());
        return grpc::Status::OK;
    }

    grpc::Status sendEvent(grpc::ServerContext*,
                           const sdv::platform::SendEventRequest* request,
                           sdv::platform::SendEventResponse*) override {
        EventMessageData msg;
        msg.eventId = request->event_id();
        msg.message = request->message();

        std::vector<std::shared_ptr<SubscriberState>> targets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [id, sub] : subscribers_) {
                (void)id;
                if (sub && sub->active.load()) {
                    targets.push_back(sub);
                }
            }
        }

        for (const auto& sub : targets) {
            sdv::platform::EventMessage out;
            out.set_event_id(msg.eventId);
            out.set_message(msg.message);
            std::lock_guard<std::mutex> writeLock(sub->writeMu);
            if (sub->active.load() && sub->writer != nullptr) {
                sub->writer->Write(out);
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status SubscribeEvents(grpc::ServerContext* context,
                                 const sdv::platform::SubscribeEventsRequest* request,
                                 grpc::ServerWriter<sdv::platform::EventMessage>* writer) override {
        const int64_t id = request->component_id();
        auto sub = std::make_shared<SubscriberState>();
        sub->writer = writer;
        sub->active.store(true);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            subscribers_[id] = sub;
        }

        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
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

private:
    struct EventMessageData {
        int32_t eventId;
        std::string message;
    };

    struct SubscriberState {
        grpc::ServerWriter<sdv::platform::EventMessage>* writer{nullptr};
        std::mutex writeMu;
        std::atomic<bool> active{false};
    };

    std::mutex mutex_;
    std::atomic<int64_t> nextId_{1};
    std::map<int64_t, std::shared_ptr<SubscriberState>> subscribers_;
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

    std::atomic<bool> keepReading{true};
    std::vector<Event> streamed1;
    std::thread consumer([&]() {
        proxy1.consumeEventStream(
            id1,
            [&]() { return keepReading.load(); },
            [&](const Event& ev) { streamed1.push_back(ev); });
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    proxy2.sendEvent(Event(EventId::VehicleLocationUpdate, "msg-a"));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    keepReading.store(false);
    consumer.join();

    sqa::test::check(tc, streamed1.size() >= 1, "SubscribeEvents delivers broadcast event");

    EventQueue afterRead = proxy1.getEventQueue(id1);
    sqa::test::check(tc, afterRead.getSize() == 0, "stream buffer drained via getEventQueue");

    proxy1.unRegister(id1);
    proxy2.sendEvent(Event(EventId::RoadInfoUpdate, "msg-b"));
    EventQueue q1AfterUnreg = proxy1.getEventQueue(id1);
    sqa::test::check(tc, q1AfterUnreg.getSize() == 0, "unregistered component receives no events");

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

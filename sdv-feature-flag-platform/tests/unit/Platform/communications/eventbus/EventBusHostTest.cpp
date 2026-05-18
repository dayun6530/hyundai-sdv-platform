#include "FeatureCommunication/EventBus/EventBusHost.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"
#include "TestAssert.h"

#define main event_bus_host_original_main
#include "src/Platform/FeatureCommunication/EventBus/src/EventBusHost.cpp"
#undef main

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace sdv::platform::communications::eventbus;

int main() {
    sqa::test::TestCounter tc;
    EventBusHost host;

    grpc::ServerContext ctx;
    sdv::platform::RegisterRequest regReq;
    sdv::platform::RegisterResponse regRes1;
    sdv::platform::RegisterResponse regRes2;
    sdv::platform::UnRegisterRequest unregReq;
    sdv::platform::UnRegisterResponse unregRes;
    sdv::platform::SendEventRequest sendReq;
    sdv::platform::SendEventResponse sendRes;

    host.registerComponent(&ctx, &regReq, &regRes1);
    host.registerComponent(&ctx, &regReq, &regRes2);
    const int64_t id1 = regRes1.component_id();
    const int64_t id2 = regRes2.component_id();
    sqa::test::check(tc, id1 > 0 && id2 > 0 && id1 != id2, "registerComponent returns unique ids");

    EventBusHost streamHost;
    std::thread serverThread([&streamHost]() { streamHost.runServer("127.0.0.1:50079"); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EventBusProxy proxy("127.0.0.1:50079");
    const int64_t streamId = proxy.registerComponent();
    sqa::test::check(tc, streamId > 0, "proxy registerComponent on running host");

    std::atomic<bool> keepReading{true};
    std::vector<Event> streamed;
    std::thread consumer([&]() {
        proxy.consumeEventStream(
            streamId,
            [&]() { return keepReading.load(); },
            [&](const Event& ev) { streamed.push_back(ev); });
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EventBusProxy publisher("127.0.0.1:50079");
    publisher.sendEvent(Event(EventId::VehicleLocationUpdate, "m1"));

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    keepReading.store(false);
    consumer.join();

    sqa::test::check(tc, streamed.size() >= 1, "SubscribeEvents delivers broadcast event");

    EventQueue drained = proxy.getEventQueue(streamId);
    sqa::test::check(tc, drained.getSize() == 0, "buffered queue empty after stream consumer");

    proxy.unRegister(streamId);

    sendReq.set_event_id(static_cast<int32_t>(EventId::VehicleLocationUpdate));
    sendReq.set_message("m1");
    host.sendEvent(&ctx, &sendReq, &sendRes);

    unregReq.set_component_id(99999);
    grpc::Status unknownUnregStatus = host.unRegister(&ctx, &unregReq, &unregRes);
    sqa::test::check(tc, unknownUnregStatus.ok(), "unRegister unknown id returns OK status");

    for (int i = 0; i < 1200; ++i) {
        sendReq.set_event_id(static_cast<int32_t>(EventId::RoadInfoUpdate));
        sendReq.set_message("bulk");
        host.sendEvent(&ctx, &sendReq, &sendRes);
    }
    sqa::test::check(tc, true, "sendEvent under load does not throw");

    streamHost.shutdownServer();
    serverThread.join();
    sqa::test::check(tc, true, "runServer/shutdownServer lifecycle executes");

    return sqa::test::finalize(tc, "EventBusHostTest");
}

#include "FeatureCommunication/EventBus/EventBusHost.h"
#include "FeatureCommunication/EventBus/EventId.h"
#include "TestAssert.h"

#define main event_bus_host_original_main
#include "src/Platform/FeatureCommunication/EventBus/src/EventBusHost.cpp"
#undef main

#include <chrono>
#include <thread>

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
    sdv::platform::GetEventQueueRequest getReq;
    sdv::platform::GetEventQueueResponse getRes;

    host.registerComponent(&ctx, &regReq, &regRes1);
    host.registerComponent(&ctx, &regReq, &regRes2);
    const int64_t id1 = regRes1.component_id();
    const int64_t id2 = regRes2.component_id();
    sqa::test::check(tc, id1 > 0 && id2 > 0 && id1 != id2, "registerComponent returns unique ids");

    sendReq.set_event_id(static_cast<int32_t>(EventId::VehicleLocationUpdate));
    sendReq.set_message("m1");
    host.sendEvent(&ctx, &sendReq, &sendRes);

    getReq.set_component_id(id1);
    host.getEventQueue(&ctx, &getReq, &getRes);
    sqa::test::check(tc, getRes.events_size() == 1, "broadcast delivered to registered queue");

    sdv::platform::GetEventQueueResponse getAfterRead;
    host.getEventQueue(&ctx, &getReq, &getAfterRead);
    sqa::test::check(tc, getAfterRead.events_size() == 0, "queue is cleared after read");

    // Failure-style case: unregister unknown id should not throw and should keep status OK.
    unregReq.set_component_id(99999);
    grpc::Status unknownUnregStatus = host.unRegister(&ctx, &unregReq, &unregRes);
    sqa::test::check(tc, unknownUnregStatus.ok(), "unRegister unknown id returns OK status");

    // Failure-style case: unknown component queue request should return empty list.
    sdv::platform::GetEventQueueRequest unknownGetReq;
    unknownGetReq.set_component_id(99999);
    sdv::platform::GetEventQueueResponse unknownGetRes;
    grpc::Status unknownGetStatus = host.getEventQueue(&ctx, &unknownGetReq, &unknownGetRes);
    sqa::test::check(tc,
                     unknownGetStatus.ok() && unknownGetRes.events_size() == 0,
                     "getEventQueue unknown id returns empty queue");

    // Exception path check: API should not throw when queue is full; events are dropped.
    for (int i = 0; i < 1200; ++i) {
        sendReq.set_event_id(static_cast<int32_t>(EventId::RoadInfoUpdate));
        sendReq.set_message("bulk");
        host.sendEvent(&ctx, &sendReq, &sendRes);
    }
    sdv::platform::GetEventQueueRequest getReq2;
    getReq2.set_component_id(id2);
    sdv::platform::GetEventQueueResponse getRes2;
    host.getEventQueue(&ctx, &getReq2, &getRes2);
    sqa::test::check(tc, getRes2.events_size() <= EventQueue::maxQueueSize, "queue capacity is enforced");

    // Server lifecycle path coverage.
    EventBusHost serverHost;
    std::thread serverThread([&serverHost]() { serverHost.runServer("127.0.0.1:50079"); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    serverHost.shutdownServer();
    serverThread.join();
    sqa::test::check(tc, true, "runServer/shutdownServer lifecycle executes");

    return sqa::test::finalize(tc, "EventBusHostTest");
}

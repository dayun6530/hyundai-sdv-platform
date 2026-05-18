#include "LifeCycleManager/FeatureEvaluator.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"
#include "MockEventBus.h"
#include "TestAssert.h"

#include <grpcpp/grpcpp.h>
#include "eventbus.grpc.pb.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define main feature_evaluator_original_main
#include "src/Platform/LifeCycleManager/src/FeatureEvaluator.cpp"
#undef main

using namespace sdv::platform::lifecyclemgr;
using Event = sdv::platform::communications::eventbus::Event;
using EventId = sdv::platform::communications::eventbus::EventId;

namespace {

int run_isolated_fork(int (*child_body)()) {
    const pid_t pid = fork();
    if (pid == 0) {
        const int code = child_body();
        _exit(code);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status)) {
        return 1;
    }
    return WEXITSTATUS(status);
}

class ThrowingSendBus final : public sqa::test::MockEventBus {
public:
    bool sendEvent(const Event&) override {
        throw std::runtime_error("sendEvent boom");
    }
};

class ThrowingQueueBus final : public sqa::test::MockEventBus {
public:
    EventQueue getEventQueue(int64_t) override {
        throw std::runtime_error("getEventQueue boom");
    }
};

class KillInjectBus final : public sqa::test::MockEventBus {
public:
    EventQueue getEventQueue(int64_t componentId) override {
        if (!killSent_[componentId]) {
            enqueueFor(componentId,
                       Event(EventId::KillSwitch, "FeatureEvaluator"));
            killSent_[componentId] = true;
        }
        return sqa::test::MockEventBus::getEventQueue(componentId);
    }

private:
    std::map<int64_t, bool> killSent_;
};

class FailingSendKillBus final : public sqa::test::MockEventBus {
public:
    bool sendEvent(const Event& event) override {
        sendCalls++;
        sentEvents.push_back(event);
        return false;
    }

    EventQueue getEventQueue(int64_t componentId) override {
        if (!killSent_[componentId]) {
            enqueueFor(componentId,
                       Event(EventId::KillSwitch, "FeatureEvaluator"));
            killSent_[componentId] = true;
        }
        return sqa::test::MockEventBus::getEventQueue(componentId);
    }

private:
    std::map<int64_t, bool> killSent_;
};

static int child_feature_evaluator_failing_send() {
    sqa::test::TestCounter tc;
    namespace fs = std::filesystem;
    fs::create_directories("tests/mockup/generated");
    const std::string inbound = "tests/mockup/generated/FeatureEvaluator_child_inbound_fail.cmd";
    const std::string outbound = "tests/mockup/generated/FeatureEvaluator_child_out_fail.cmd";
    {
        std::ofstream in(inbound, std::ios::trunc);
        in << "FLAG_ON:AdasFeature\n";
    }
    std::ofstream(outbound, std::ios::trunc).close();

    FailingSendKillBus bus;
    FeatureEvaluator evaluator(bus, inbound, outbound);
    bool noThrow = true;
    try {
        evaluator.run();
    } catch (...) {
        noThrow = false;
    }
    sqa::test::check(tc, noThrow,
                     "FeatureEvaluator does not throw when sendEvent returns false");
    sqa::test::check(tc, bus.sendCalls >= 1,
                     "FeatureEvaluator invokes sendEvent for FLAG_ON");
    std::ifstream after(inbound);
    std::string rest;
    std::getline(after, rest);
    sqa::test::check(tc, rest.empty(),
                     "FeatureEvaluator clears inbound after sendEvent failure");

    return sqa::test::finalize(tc, "FeatureEvaluatorTest (isolated fail-send)");
}

static int child_feature_evaluator_missing_inbound() {
    sqa::test::TestCounter tc;
    namespace fs = std::filesystem;
    fs::create_directories("tests/mockup/generated");
    const std::string inbound =
        "tests/mockup/generated/__no_such_subdir__/FeatureEvaluator_inbound.cmd";
    const std::string outbound =
        "tests/mockup/generated/FeatureEvaluator_child_out_missing.cmd";
    std::ofstream(outbound, std::ios::trunc).close();

    KillInjectBus bus;
    FeatureEvaluator evaluator(bus, inbound, outbound);
    bool noThrow = true;
    try {
        evaluator.run();
    } catch (...) {
        noThrow = false;
    }
    sqa::test::check(tc, noThrow,
                     "FeatureEvaluator missing inbound path does not throw");
    sqa::test::check(tc, bus.unRegisterCalls == 1,
                     "FeatureEvaluator unregisters after kill on missing inbound");

    return sqa::test::finalize(tc, "FeatureEvaluatorTest (isolated missing inbound)");
}

static int child_feature_evaluator_invalid_then_valid() {
    sqa::test::TestCounter tc;
    namespace fs = std::filesystem;
    fs::create_directories("tests/mockup/generated");
    const std::string inbound =
        "tests/mockup/generated/FeatureEvaluator_child_inbound_invalid.cmd";
    const std::string outbound =
        "tests/mockup/generated/FeatureEvaluator_child_out_invalid.cmd";
    {
        std::ofstream in(inbound, std::ios::trunc);
        in << "no_colon_token\n";
        in << "FLAG_ON:ZZZFeature\n";
    }
    std::ofstream(outbound, std::ios::trunc).close();

    KillInjectBus bus;
    FeatureEvaluator evaluator(bus, inbound, outbound);
    bool noThrow = true;
    try {
        evaluator.run();
    } catch (...) {
        noThrow = false;
    }
    sqa::test::check(tc, noThrow,
                     "FeatureEvaluator invalid line then valid line does not throw");
    bool sawZ = false;
    for (const auto& e : bus.sentEvents) {
        if (e.getMessage() == "ZZZFeature" &&
            e.getEventId() == EventId::FeatureFlagOn) {
            sawZ = true;
            break;
        }
    }
    sqa::test::check(tc, sawZ,
                     "FeatureEvaluator still processes valid FLAG_ON after bad line");

    return sqa::test::finalize(tc, "FeatureEvaluatorTest (isolated invalid format)");
}

class FakeEventBusService final : public sdv::platform::EventBus::Service {
public:
    grpc::Status registerComponent(grpc::ServerContext*,
                                   const sdv::platform::RegisterRequest*,
                                   sdv::platform::RegisterResponse* response) override {
        response->set_component_id(100);
        return grpc::Status::OK;
    }

    grpc::Status unRegister(grpc::ServerContext*,
                            const sdv::platform::UnRegisterRequest*,
                            sdv::platform::UnRegisterResponse*) override {
        return grpc::Status::OK;
    }

    grpc::Status sendEvent(grpc::ServerContext*,
                           const sdv::platform::SendEventRequest* request,
                           sdv::platform::SendEventResponse*) override {
        std::lock_guard<std::mutex> lock(mutex_);
        received_.push_back({request->event_id(), request->message()});
        return grpc::Status::OK;
    }

    grpc::Status SubscribeEvents(grpc::ServerContext* context,
                                 const sdv::platform::SubscribeEventsRequest*,
                                 grpc::ServerWriter<sdv::platform::EventMessage>*) override {
        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return grpc::Status::OK;
    }

    int receivedCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(received_.size());
    }

private:
    struct EventRow {
        int32_t id;
        std::string message;
    };
    mutable std::mutex mutex_;
    std::vector<EventRow> received_;
};

}  // namespace

int main() {
    sqa::test::TestCounter tc;
    namespace fs = std::filesystem;

    fs::create_directories("tests/mockup/generated");

    // --- Exception / failure paths (MockEventBus), before gRPC + requestStop ---
    {
        const std::string inboundThrow =
            "tests/mockup/generated/FeatureEvaluator_inbound_throw_send.cmd";
        const std::string outboundThrow =
            "tests/mockup/generated/FeatureEvaluator_out_throw_send.cmd";
        {
            std::ofstream in(inboundThrow, std::ios::trunc);
            in << "FLAG_ON:ThrowTarget\n";
        }
        std::ofstream(outboundThrow, std::ios::trunc).close();
        ThrowingSendBus throwSendBus;
        FeatureEvaluator evalThrow(throwSendBus, inboundThrow, outboundThrow);
        bool caughtSend = false;
        try {
            evalThrow.run();
        } catch (const std::runtime_error&) {
            caughtSend = true;
        } catch (...) {
        }
        sqa::test::check(tc, caughtSend,
                         "FeatureEvaluator propagates exception from sendEvent");
    }

    {
        const std::string inboundQ =
            "tests/mockup/generated/FeatureEvaluator_inbound_throw_queue.cmd";
        const std::string outboundQ =
            "tests/mockup/generated/FeatureEvaluator_out_throw_queue.cmd";
        std::ofstream(inboundQ, std::ios::trunc).close();
        std::ofstream(outboundQ, std::ios::trunc).close();
        ThrowingQueueBus throwQueueBus;
        FeatureEvaluator evalQueue(throwQueueBus, inboundQ, outboundQ);
        bool caughtQueue = false;
        try {
            evalQueue.run();
        } catch (const std::runtime_error&) {
            caughtQueue = true;
        } catch (...) {
        }
        sqa::test::check(tc, caughtQueue,
                         "FeatureEvaluator propagates exception from getEventQueue");
    }

    sqa::test::check(tc, run_isolated_fork(child_feature_evaluator_failing_send) == 0,
                     "isolated child: sendEvent false path");
    sqa::test::check(tc, run_isolated_fork(child_feature_evaluator_missing_inbound) == 0,
                     "isolated child: missing inbound path");
    sqa::test::check(tc, run_isolated_fork(child_feature_evaluator_invalid_then_valid) == 0,
                     "isolated child: invalid format then FLAG_ON");

    const std::string inbound = "tests/mockup/generated/FeatureEvaluatorInbound.cmd";
    const std::string outbound = "tests/mockup/generated/FeatureEvaluatorOutbound.cmd";

    {
        std::ofstream in(inbound, std::ios::trunc);
        in << "FLAG_ON:AdasFeature\n";
        in << "FLAG_OFF:EvEnergyFeature\n";
        in << "KILL:NavigationFeature\n";
        in << "AB_SET:NavigationFeature:A\n";
        in << "UNKNOWN:Whatever\n";
    }
    std::ofstream(outbound, std::ios::trunc).close();

    FakeEventBusService service;
    grpc::ServerBuilder builder;
    int selectedPort = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selectedPort);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    std::thread serverThread([&server]() { server->Wait(); });

    const std::string address = "127.0.0.1:" + std::to_string(selectedPort);
    sdv::platform::communications::eventbus::EventBusProxy proxy(address);
    FeatureEvaluator evaluator(proxy, inbound, outbound);

    std::thread evalThread([&evaluator]() { evaluator.run(); });
    // Allow initial 3s registration delay + auto SHADOW_ON + first poll loop iteration (processSignals).
    std::this_thread::sleep_for(std::chrono::milliseconds(4000));
    Feature::requestStop();
    evalThread.join();

    server->Shutdown();
    serverThread.join();

    sqa::test::check(tc, service.receivedCount() >= 4, "FeatureEvaluator sends events to EventBus (including AB_SET)");

    std::ifstream inAfter(inbound);
    std::string rest;
    std::getline(inAfter, rest);
    sqa::test::check(tc, rest.empty(), "FeatureEvaluator clears inbound command file");

    std::ifstream outCheck(outbound);
    std::string outLine;
    bool warningFound = false;
    while (std::getline(outCheck, outLine)) {
        if (outLine.find("WARNING:NavigationFeature_A:KillSwitch activated") != std::string::npos ||
            outLine.find("WARNING:NavigationFeature_B:KillSwitch activated") != std::string::npos) {
            warningFound = true;
            break;
        }
    }
    sqa::test::check(tc, warningFound, "FeatureEvaluator writes warning for kill switch");

    return sqa::test::finalize(tc, "FeatureEvaluatorTest");
}

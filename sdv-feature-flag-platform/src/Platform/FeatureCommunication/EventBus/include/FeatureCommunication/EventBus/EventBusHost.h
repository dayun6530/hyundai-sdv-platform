#pragma once

#include "FeatureCommunication/EventBus/EventQueue.h"

#include <grpcpp/grpcpp.h>
#include "eventbus.grpc.pb.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sdv::platform::communications::eventbus {

class EventBusHost final : public sdv::platform::EventBus::Service {
public:
    EventBusHost();

    grpc::Status registerComponent(grpc::ServerContext* context,
                                   const RegisterRequest* request,
                                   RegisterResponse* response) override;

    grpc::Status unRegister(grpc::ServerContext* context,
                            const UnRegisterRequest* request,
                            UnRegisterResponse* response) override;

    grpc::Status sendEvent(grpc::ServerContext* context,
                           const SendEventRequest* request,
                           SendEventResponse* response) override;

    grpc::Status SubscribeEvents(grpc::ServerContext* context,
                                   const SubscribeEventsRequest* request,
                                   grpc::ServerWriter<EventMessage>* writer) override;

    void runServer(const std::string& address);
    void shutdownServer();

private:
    struct StreamSubscriber {
        grpc::ServerWriter<EventMessage>* writer{nullptr};
        std::mutex writeMu;
        std::atomic<bool> active{false};
    };

    bool pushEventToStream(int64_t componentId, const Event& event);
    bool writeEventToSubscriber(const std::shared_ptr<StreamSubscriber>& sub, const Event& event);
    void flushQueueToStream(EventQueue& queue, const std::shared_ptr<StreamSubscriber>& sub);

    std::mutex mutex_;
    std::atomic<int64_t> nextComponentId_;
    std::vector<EventQueue> eventQueueList_;
    std::unordered_map<int64_t, std::shared_ptr<StreamSubscriber>> subscribers_;
    std::unique_ptr<grpc::Server> server_;
};

}  // namespace sdv::platform::communications::eventbus

#pragma once

#include "FeatureCommunication/EventBus/EventQueue.h"

#include <grpcpp/grpcpp.h>
#include "eventbus.grpc.pb.h"

#include <vector>
#include <mutex>
#include <memory>
#include <string>
#include <atomic>

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

    grpc::Status getEventQueue(grpc::ServerContext* context,
                               const GetEventQueueRequest* request,
                               GetEventQueueResponse* response) override;

    void runServer(const std::string& address);
    void shutdownServer();

private:
    std::mutex mutex_;
    std::atomic<int64_t> nextComponentId_;
    std::vector<EventQueue> eventQueueList_;
    std::unique_ptr<grpc::Server> server_;
};

}  // namespace sdv::platform::communications::eventbus

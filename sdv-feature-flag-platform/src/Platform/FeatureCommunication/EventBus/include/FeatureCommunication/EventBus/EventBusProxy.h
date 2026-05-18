#pragma once

#include "FeatureCommunication/EventBus/EventBusInterface.h"

#include <grpcpp/grpcpp.h>
#include "eventbus.grpc.pb.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace sdv::platform::communications::eventbus {

class EventBusProxy : public EventBusInterface {
public:
    explicit EventBusProxy(const std::string& address);
    ~EventBusProxy() override;

    int64_t registerComponent() override;
    void unRegister(int64_t componentId) override;
    bool sendEvent(const Event& event) override;
    EventQueue getEventQueue(int64_t componentId) override;

    /// Blocks on the SubscribeEvents stream until \p shouldContinue returns false or the stream ends.
    /// Invokes \p onEvent for each received event (used by DataCollector; no polling sleep).
    void consumeEventStream(int64_t componentId,
                            const std::function<bool()>& shouldContinue,
                            const std::function<void(const Event&)>& onEvent);

private:
    void startStreamReader(int64_t componentId);
    void stopStreamReader(int64_t componentId);
    void appendBufferedEvent(int64_t componentId, const Event& event);

    std::unique_ptr<sdv::platform::EventBus::Stub> stub_;

    std::mutex streamMutex_;
    struct ComponentStreamState {
        std::thread reader;
        std::unique_ptr<grpc::ClientContext> context;
        std::atomic<bool> running{false};
        std::deque<Event> buffer;
        std::mutex bufferMu;
    };
    std::unordered_map<int64_t, std::unique_ptr<ComponentStreamState>> streamReaders_;
};

}  // namespace sdv::platform::communications::eventbus

// eventHandler.hpp
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <cstdint>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "unleash/Domain/context.hpp"

namespace unleash {

enum class ClientEvent : std::uint8_t { Init, Error, Ready, Update, Impression };

class EventHandler final {
  public:
    struct ClientError final {
        std::string message;
        std::string details;
    };
    struct ClientImpression final {
        Context ctx;
        std::string flagName;
        bool enabled;
        std::string eventType;
        bool impressionData;
        std::string variant = "";
    };

    using InitCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const ClientError&)>;
    using ReadyCallback = std::function<void()>;
    using UpdateCallback = std::function<void()>;
    using ImpressionCallback = std::function<void(const ClientImpression&)>;

    EventHandler();
    ~EventHandler();

    EventHandler(const EventHandler&) = delete;
    EventHandler& operator=(const EventHandler&) = delete;

    // Start/stop the event dispatch thread
    void start();
    void stop();

    // Callback setters
    void onInit(InitCallback cb);
    void onError(ErrorCallback cb);
    void onReady(ReadyCallback cb);
    void onUpdate(UpdateCallback cb);
    void onImpression(ImpressionCallback cb);

    void emitInit() const;
    void emitError(const ClientError& err) const;
    void emitReady() const;
    void emitUpdate() const;
    void emitImpression(const ClientImpression& event) const;

    void clearAll();

  private:
    // event dispatch thread routine
    void eventLoop();

    using EventTask = std::function<void()>;

    mutable std::queue<EventTask> _eventQueue;
    mutable std::mutex _queueMutex;
    mutable std::condition_variable _queueCV;

    // Event dispatch thread
    std::thread _eventThread;
    std::atomic<bool> _started{false};
    std::atomic<bool> _running{false};

    // Callback storage:
    mutable std::shared_ptr<InitCallback> _initCb;
    mutable std::shared_ptr<ErrorCallback> _errorCb;
    mutable std::shared_ptr<ReadyCallback> _readyCb;
    mutable std::shared_ptr<UpdateCallback> _updateCb;
    mutable std::shared_ptr<ImpressionCallback> _impressionCb;
};

} // namespace unleash

#include "unleash/EventHandler/eventHandler.hpp"
#include "unleash/Utils/utils.hpp"
#include <atomic>
#include <iostream>

namespace unleash {

EventHandler::EventHandler() = default;

EventHandler::~EventHandler() {
    stop();
}

void EventHandler::start() {
    if (_started.exchange(true)) {
        return;
    }
    _eventThread = std::thread(&EventHandler::eventLoop, this);
}

void EventHandler::stop() {
    if (!_started.exchange(false)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        std::queue<EventTask> empty;
        _eventQueue.swap(empty); // Drop pending tasks:
    }

    _queueCV.notify_all();

    if (_eventThread.joinable()) {
        _eventThread.join();
    }
}

void EventHandler::eventLoop() {
    while (_started.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lock(_queueMutex);

        _queueCV.wait(lock, [this] { return !_eventQueue.empty() || !_started.load(std::memory_order_acquire); });

        while (!_eventQueue.empty()) {
            auto task = std::move(_eventQueue.front());
            _eventQueue.pop();
            lock.unlock();

            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "EventHandler: Exception in callback: " << e.what() << '\n';
            } catch (...) {
                std::cerr << "EventHandler: Unknown exception in callback\n";
            }

            lock.lock();
        }
    }
}

void EventHandler::onInit(InitCallback cb) {
    auto ptr = cb ? std::make_shared<InitCallback>(std::move(cb)) : std::shared_ptr<InitCallback>{};
    std::atomic_store_explicit(&_initCb, ptr, std::memory_order_release);
}

void EventHandler::onError(ErrorCallback cb) {
    auto ptr = cb ? std::make_shared<ErrorCallback>(std::move(cb)) : std::shared_ptr<ErrorCallback>{};
    std::atomic_store_explicit(&_errorCb, ptr, std::memory_order_release);
}

void EventHandler::onReady(ReadyCallback cb) {
    auto ptr = cb ? std::make_shared<ReadyCallback>(std::move(cb)) : std::shared_ptr<ReadyCallback>{};
    std::atomic_store_explicit(&_readyCb, ptr, std::memory_order_release);
}

void EventHandler::onUpdate(UpdateCallback cb) {
    auto ptr = cb ? std::make_shared<UpdateCallback>(std::move(cb)) : std::shared_ptr<UpdateCallback>{};
    std::atomic_store_explicit(&_updateCb, ptr, std::memory_order_release);
}

void EventHandler::onImpression(ImpressionCallback cb) {
    auto ptr = cb ? std::make_shared<ImpressionCallback>(std::move(cb)) : std::shared_ptr<ImpressionCallback>{};
    std::atomic_store_explicit(&_impressionCb, ptr, std::memory_order_release);
}

void EventHandler::clearAll() {
    std::atomic_store_explicit(&_initCb, std::shared_ptr<InitCallback>{}, std::memory_order_release);
    std::atomic_store_explicit(&_errorCb, std::shared_ptr<ErrorCallback>{}, std::memory_order_release);
    std::atomic_store_explicit(&_readyCb, std::shared_ptr<ReadyCallback>{}, std::memory_order_release);
    std::atomic_store_explicit(&_updateCb, std::shared_ptr<UpdateCallback>{}, std::memory_order_release);
    std::atomic_store_explicit(&_impressionCb, std::shared_ptr<ImpressionCallback>{}, std::memory_order_release);
}

void EventHandler::emitInit() const {
    if (!_started.load(std::memory_order_acquire)) {
        return;
    }

    auto cb = std::atomic_load_explicit(&_initCb, std::memory_order_acquire);
    if (cb && *cb) {
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            if (_eventQueue.size() < utils::maxEventQueueSize) {
                _eventQueue.push([cb]() { (*cb)(); });
            }
        }
        _queueCV.notify_one();
    }
}

void EventHandler::emitError(const ClientError& err) const {
    if (!_started.load(std::memory_order_acquire)) {
        return;
    }

    auto cb = std::atomic_load_explicit(&_errorCb, std::memory_order_acquire);
    if (cb && *cb) {
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            if (_eventQueue.size() < utils::maxEventQueueSize) {
                _eventQueue.push([cb, err]() { (*cb)(err); });
            }
        }
        _queueCV.notify_one();
    }
}

void EventHandler::emitReady() const {
    if (!_started.load(std::memory_order_acquire)) {
        return;
    }

    auto cb = std::atomic_load_explicit(&_readyCb, std::memory_order_acquire);
    if (cb && *cb) {
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            if (_eventQueue.size() < utils::maxEventQueueSize) {
                _eventQueue.push([cb]() { (*cb)(); });
            }
        }
        _queueCV.notify_one();
    }
}

void EventHandler::emitUpdate() const {
    if (!_started.load(std::memory_order_acquire)) {
        return;
    }

    auto cb = std::atomic_load_explicit(&_updateCb, std::memory_order_acquire);
    if (cb && *cb) {
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            if (_eventQueue.size() < utils::maxEventQueueSize) {
                _eventQueue.push([cb]() { (*cb)(); });
            }
        }
        _queueCV.notify_one();
    }
}

void EventHandler::emitImpression(const ClientImpression& event) const {
    if (!_started.load(std::memory_order_acquire)) {
        return;
    }

    auto cb = std::atomic_load_explicit(&_impressionCb, std::memory_order_acquire);
    if (cb && *cb) {
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            if (_eventQueue.size() < utils::maxEventQueueSize) {
                _eventQueue.push([cb, event]() { (*cb)(event); });
            }
        }
        _queueCV.notify_one();
    }
}

} // namespace unleash
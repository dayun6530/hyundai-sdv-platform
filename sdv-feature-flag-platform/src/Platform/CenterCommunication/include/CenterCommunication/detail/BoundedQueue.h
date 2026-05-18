#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>

namespace sdv::platform::centercommunication::detail {

/// Thread-safe bounded queue. When full, drops the oldest item and increments \c dropCount().
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity > 0 ? capacity : 1) {}

    void push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return;
        }
        if (items_.size() >= capacity_) {
            items_.pop_front();
            ++drops_;
        }
        items_.push_back(std::move(item));
        cv_.notify_one();
    }

    /// Blocks up to \p timeout for an item. Returns false on timeout with no item available.
    bool pop(T& out, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this] { return !items_.empty() || closed_; })) {
            return false;
        }
        if (items_.empty()) {
            return false;
        }
        out = std::move(items_.front());
        items_.pop_front();
        return true;
    }

    /// Non-blocking pop; returns false if empty.
    bool tryPop(T& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (items_.empty()) {
            return false;
        }
        out = std::move(items_.front());
        items_.pop_front();
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cv_.notify_all();
    }

    bool isClosed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.empty();
    }

    std::uint64_t dropCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return drops_;
    }

private:
    std::size_t capacity_;
    std::deque<T> items_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool closed_{false};
    std::uint64_t drops_{0};
};

}  // namespace sdv::platform::centercommunication::detail

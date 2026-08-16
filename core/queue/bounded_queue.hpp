#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace swave::core {

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    [[nodiscard]] bool try_push(T value) {
        std::lock_guard lock(mutex_);
        if (closed_ || queue_.size() >= capacity_) {
            return false;
        }
        queue_.push_back(std::move(value));
        available_.notify_one();
        return true;
    }

    [[nodiscard]] bool try_pop(T& value) {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        value = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        available_.notify_all();
    }

    void reset() {
        std::lock_guard lock(mutex_);
        queue_.clear();
        closed_ = false;
        available_.notify_all();
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool closed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::deque<T> queue_;
    bool closed_{};
};

} // namespace swave::core

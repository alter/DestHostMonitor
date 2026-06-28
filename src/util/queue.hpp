#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace pt {

// Simple multi-producer / multi-consumer blocking queue.
// Producers push(); consumers pop() and block until an item is available or the
// queue is closed and drained. After close(), pop() returns remaining items then
// std::nullopt forever.
template <typename T>
class BlockingQueue {
public:
    void push(T value) {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push_back(std::move(value));
        }
        cv_.notify_one();
    }

    // Blocks until an item is available, or returns nullopt once closed & empty.
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return !q_.empty() || closed_; });
        if (q_.empty()) return std::nullopt;
        T v = std::move(q_.front());
        q_.pop_front();
        return v;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lk(m_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(m_);
        return q_.size();
    }

private:
    mutable std::mutex      m_;
    std::condition_variable cv_;
    std::deque<T>           q_;
    bool                    closed_ = false;
};

}  // namespace pt

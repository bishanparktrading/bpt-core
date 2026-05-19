#pragma once

/// \file
/// \brief Mutex+CV+deque queue: main poll thread → per-adapter send-executor.
///
/// SPSC in practice (main is the sole producer; executor is the sole
/// consumer), but a mutex is fine at ogw rates: 10s/sec, contention is
/// negligible and we avoid the cost of designing yet another lockfree
/// slot. The CV lets the executor sleep until work arrives instead of
/// busy-polling.

#include "order_gateway/util/send_work_item.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace bpt::order_gateway::util {

class SendWorkQueue {
public:
    /// Producer (main poll thread). Non-blocking.
    void push(SendWorkItem item) {
        {
            std::lock_guard<std::mutex> g(mu_);
            items_.push_back(std::move(item));
        }
        cv_.notify_one();
    }

    /// Consumer (send-executor thread). Blocks until an item is
    /// available or close() is called. Returns std::nullopt if closed
    /// and queue is drained.
    std::optional<SendWorkItem> pop_blocking() {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this] { return closed_.load(std::memory_order_acquire) || !items_.empty(); });
        if (items_.empty())
            return std::nullopt;
        SendWorkItem out = std::move(items_.front());
        items_.pop_front();
        return out;
    }

    /// Wakes any consumer waiting in pop_blocking() and causes subsequent
    /// pop_blocking() calls to return nullopt once drained.
    void close() {
        closed_.store(true, std::memory_order_release);
        cv_.notify_all();
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<SendWorkItem> items_;
    std::atomic<bool> closed_{false};
};

}  // namespace bpt::order_gateway::util

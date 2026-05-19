#pragma once

// Typed lock-free SPSC ring buffer for ExecEvent.
// One queue per adapter: adapter IO thread pushes, main poll thread pops.
//
// Capacity is set at construction time and must be a power of 2. Slots
// are heap-allocated once in the ctor and never resized. The extra
// pointer indirection (vs a stack-inline template buffer) is irrelevant
// at ExecEvent rates — event throughput is bounded by exchange ack
// latency and tops out at a few hundred events per second even on a
// busy venue. The gain is that the capacity becomes an AdapterConfig
// knob (`exec_queue_capacity`) instead of a recompile.

#include "order_gateway/adapter/common/i_order_adapter.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace bpt::order_gateway::util {

class ExecEventQueue {
public:
    explicit ExecEventQueue(std::size_t capacity)
        : mask_(capacity - 1),
          slots_(std::make_unique<adapter::ExecEvent[]>(capacity)) {
        if (capacity == 0 || (capacity & (capacity - 1)) != 0)
            throw std::invalid_argument("ExecEventQueue: capacity must be a power of 2");
    }

    ExecEventQueue(const ExecEventQueue&) = delete;
    ExecEventQueue& operator=(const ExecEventQueue&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return mask_ + 1; }

    // Producer thread only.
    [[nodiscard]] bool try_push(const adapter::ExecEvent& ev) noexcept {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) > mask_)
            return false;
        slots_[h & mask_] = ev;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread only.
    template <typename Fn>
    bool try_pop(Fn&& fn) noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (head_.load(std::memory_order_acquire) == t)
            return false;
        fn(slots_[t & mask_]);
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    const std::size_t mask_;
    std::unique_ptr<adapter::ExecEvent[]> slots_;
};

/// \brief MPSC variant — same ring, mutex-serialised producers.
///
/// Used for `rest_exec_queue_` where the send-executor thread is the
/// primary producer, but secondary producers can fire on rare paths
/// (e.g. the HL reconciler worker thread emits recovered ExecEvents).
/// Pop side remains lockless (single consumer = main poll thread).
///
/// Cost is one mutex acquisition per push; at ogw event rates (~10s/sec)
/// this is negligible and the queue stays correct under multi-producer.
class MpscExecEventQueue {
public:
    explicit MpscExecEventQueue(std::size_t capacity) : q_(capacity) {}

    MpscExecEventQueue(const MpscExecEventQueue&) = delete;
    MpscExecEventQueue& operator=(const MpscExecEventQueue&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return q_.capacity(); }

    /// Producer side. Any thread; multiple producers serialise on `mu_`.
    [[nodiscard]] bool try_push(const adapter::ExecEvent& ev) noexcept {
        std::lock_guard<std::mutex> g(mu_);
        return q_.try_push(ev);
    }

    /// Consumer side — single thread. Lockless.
    template <typename Fn>
    bool try_pop(Fn&& fn) noexcept {
        return q_.try_pop(std::forward<Fn>(fn));
    }

private:
    std::mutex mu_;
    ExecEventQueue q_;
};

}  // namespace bpt::order_gateway::util

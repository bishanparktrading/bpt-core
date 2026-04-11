#pragma once

// Typed lock-free SPSC ring buffer for ExecEvent.
// One queue per adapter: adapter IO thread pushes, main poll thread pops.

#include "heimdall/adapter/common/i_order_adapter.h"

#include <atomic>
#include <cstddef>

namespace heimdall::util {

// N must be a power of 2.  Sized for a realistic burst: 256 events in flight
// before the main thread drains is far beyond any exchange's event rate.
template <std::size_t N>
class ExecEventQueue {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");

public:
    // Producer thread only.
    [[nodiscard]] bool try_push(const adapter::ExecEvent& ev) noexcept {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) >= N)
            return false;
        slots_[h & (N - 1)] = ev;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread only.
    template <typename Fn>
    bool try_pop(Fn&& fn) noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (head_.load(std::memory_order_acquire) == t)
            return false;
        fn(slots_[t & (N - 1)]);
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    adapter::ExecEvent slots_[N];
};

}  // namespace heimdall::util

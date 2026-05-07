#pragma once

/// \file
/// \brief Lock-free single-producer / single-consumer ring buffer for raw byte frames.
///
/// Typical use: decouple a WebSocket IO thread (producer) from a
/// parser/publisher thread (consumer). The IO thread stamps recv_ns
/// and enqueues the raw payload; the consumer drains, parses, and
/// offers to Aeron — never blocking the IO thread on Aeron
/// back-pressure.
///
/// Dependencies: none (C++ stdlib only).
///
/// \code
///   bpt::common::util::SpscQueue<512, 16384> q;
///   // producer thread:
///   q.try_push(recv_ns, payload);
///   // consumer thread:
///   q.try_pop([](uint64_t recv_ns, std::string_view data) { ... });
/// \endcode

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace bpt::common::util {

/// \brief Lock-free SPSC ring buffer of fixed-capacity slots, each holding
///        a recv_ns timestamp and an inline byte payload.
///
/// Safe under exactly one producer thread calling try_push() and
/// exactly one consumer thread calling try_pop(). Synchronization is
/// via a head/tail pair of acquire/release atomics, both cache-line
/// isolated to avoid false sharing between the two threads.
///
/// Memory: CAPACITY * (16 + MAX_PAYLOAD_BYTES) bytes for the slot
/// array, plus two cache-line-aligned atomic counters. E.g. 512 slots
/// × 16 KiB ≈ 8 MiB per queue.
///
/// \tparam CAPACITY            number of slots; must be a power of 2
/// \tparam MAX_PAYLOAD_BYTES   per-slot byte limit; oversized frames are dropped at try_push()
template <size_t CAPACITY, size_t MAX_PAYLOAD_BYTES>
class SpscQueue {
    static_assert((CAPACITY & (CAPACITY - 1)) == 0, "CAPACITY must be a power of 2");
    static constexpr size_t MASK = CAPACITY - 1;

    struct Slot {
        uint64_t recv_ns{0};
        size_t len{0};
        char data[MAX_PAYLOAD_BYTES];
    };

public:
    SpscQueue() = default;

    /// \brief Enqueue a frame. Producer-only; do not call from the consumer thread.
    ///
    /// \param recv_ns   wall-time receive timestamp captured by the producer
    /// \param payload   bytes to copy into the slot; must fit in MAX_PAYLOAD_BYTES
    /// \return true on success; false if the queue is full or the payload is oversized
    [[nodiscard]] bool try_push(uint64_t recv_ns, std::string_view payload) noexcept {
        if (payload.size() > MAX_PAYLOAD_BYTES)
            return false;
        const size_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) >= CAPACITY)
            return false;
        Slot& s = slots_[h & MASK];
        s.recv_ns = recv_ns;
        s.len = payload.size();
        std::memcpy(s.data, payload.data(), payload.size());
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    /// \brief Dequeue one frame and pass it to fn while the slot is still owned.
    ///        Consumer-only; do not call from the producer thread.
    ///
    /// fn is invoked with `(uint64_t recv_ns, std::string_view data)`. The
    /// string_view is valid only for the duration of the call — the slot is
    /// released to the producer immediately after fn returns, so any data
    /// the consumer needs to keep must be copied out.
    ///
    /// \param fn   callable invoked with the dequeued slot's contents
    /// \return true if a frame was consumed; false if the queue is empty
    template <typename Fn>
    bool try_pop(Fn&& fn) noexcept {
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (head_.load(std::memory_order_acquire) == t)
            return false;
        const Slot& s = slots_[t & MASK];
        fn(s.recv_ns, std::string_view(s.data, s.len));
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    /// \brief Compile-time slot count.
    static constexpr size_t capacity() noexcept { return CAPACITY; }
    /// \brief Compile-time per-slot payload limit.
    static constexpr size_t max_payload_bytes() noexcept { return MAX_PAYLOAD_BYTES; }

private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    Slot slots_[CAPACITY];
};

}  // namespace bpt::common::util

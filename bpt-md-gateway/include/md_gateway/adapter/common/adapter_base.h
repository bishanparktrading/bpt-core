#pragma once

/// \file
/// \brief Common base class for bpt-md-gateway venue adapters.
///
/// AdapterBase owns the IO + publisher threads and the WS reconnect loop.
/// Each venue subclass plugs in connect/read/parse via the protected
/// virtual hooks. bpt-tape reuses the same adapter library by
/// substituting recording-aware on_frame() overrides on derived classes.
///
/// Templated on Pub (the concrete publisher type) so the hot path
///     decoder → md_pub_->publish()
/// is vtable-free. Prod md-gateway instantiates AdapterBase<MdPublisher>;
/// bpt-tape instantiates AdapterBase<NoopMdPublisher>; tests can use any
/// concrete type satisfying the publisher signature. The MdPublisher
/// itself now owns validation + drop-rate breaker state (one publisher
/// per adapter — validator state is publisher-thread-confined).

#include "md_gateway/adapter/common/i_adapter.h"
#include "md_gateway/adapter/common/subscription_map.h"
#include "md_gateway/config/settings.h"
#include "md_gateway/messaging/publishers/api/funding_rate_publisher.h"
#include "md_gateway/messaging/publishers/api/instrument_stats_publisher.h"

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <bpt_common/logging.h>
#include <bpt_common/util/spsc_queue.h>
#include <bpt_common/util/strings.h>
#include <bpt_common/util/thread_name.h>
#include <bpt_common/util/thread_pin.h>
#include <bpt_common/ws/ws_connect.h>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace bpt::md_gateway::adapter {

namespace detail {

// Compose the topology role name used by md-gateway IO threads:
// "mdgw.<venue-lower>.io". Keeps the role vocabulary in sync with
// the service_name abbreviation used elsewhere (bpt-mdgw-<venue>).
inline std::string io_role(const char* exchange) {
    return "mdgw." + bpt::common::util::to_lower(exchange) + ".io";
}

// OS thread names for the two AdapterBase threads. Venue in the middle
// so sort order groups all threads of the same venue together in ps -L
// (mdgw-okx-io, mdgw-okx-log, mdgw-okx-pub sit adjacent alphabetically).
// Matches the existing quill-backend (mdgw-<venue>-log) and topology-role
// (mdgw.<venue>.<subsystem>) ordering. 15-char cap per Linux TASK_COMM_LEN.
inline std::string io_thread_name(const char* exchange) {
    return "mdgw-" + bpt::common::util::to_lower(exchange) + "-io";
}
inline std::string pub_thread_name(const char* exchange) {
    return "mdgw-" + bpt::common::util::to_lower(exchange) + "-pub";
}

}  // namespace detail

/// \brief Subscription command crossed from control thread to publisher thread.
///
/// IAdapter::subscribe() and unsubscribe() used to mutate SubscriptionMap
/// directly from the control thread (under a shared_mutex). Single-writer
/// principle: only one thread should ever write to a piece of state.
/// Now the control thread enqueues a SubCmd; the publisher thread drains
/// the queue at the top of every publish_loop iteration and applies the
/// commands to subs_ — making the publisher thread the sole writer.
struct SubCmd {
    enum class Op : uint8_t { Subscribe, Unsubscribe };
    Op op{Op::Subscribe};
    uint64_t instrument_id{0};
    std::string symbol;
    uint8_t depth{0};
};

/// \brief Thread-safe pending-command buffer for cross-thread subscription updates.
///
/// Mutex+deque rather than lock-free because subscribe/unsubscribe events
/// are rare (~ once per session) — mutex contention is effectively zero.
/// Producer side: control thread (push). Consumer side: publisher thread
/// (drain). Locking only happens during the brief push or the
/// swap-then-drain in the consumer; the consumer holds no lock while
/// processing the swapped-out commands.
class SubCmdQueue {
public:
    void push(SubCmd cmd) {
        std::lock_guard<std::mutex> lk(mu_);
        q_.push_back(std::move(cmd));
        pending_.store(true, std::memory_order_release);
    }

    /// \brief Drain all queued commands and invoke `f(SubCmd&&)` on each.
    ///
    /// Fast path when the queue is empty: a single atomic load (no mutex).
    /// On the publisher's hot spin loop this is essentially free. When
    /// there's work, the lock is held only long enough to swap the deque
    /// out; the application callback runs lock-free. Returns the number
    /// drained.
    template <class F>
    size_t drain(F&& f) {
        if (!pending_.load(std::memory_order_acquire))
            return 0;
        std::deque<SubCmd> tmp;
        {
            std::lock_guard<std::mutex> lk(mu_);
            tmp.swap(q_);
            pending_.store(false, std::memory_order_release);
        }
        for (auto& c : tmp)
            f(std::move(c));
        return tmp.size();
    }

private:
    std::mutex mu_;
    std::deque<SubCmd> q_;
    // Producer→consumer signal. Lets the consumer skip the mutex when
    // there's nothing to do — the common case on the hot loop.
    std::atomic<bool> pending_{false};
};

/// \brief Base class for every exchange market-data adapter.
///
/// Owns the common lifecycle state — io_context, ssl_context, subscription
/// map, stop flag — and two threads:
///   - **IO thread** drives WebSocket receive, stamps recv_ns, pushes raw
///     frames into frame_queue_.
///   - **Publisher thread** drains frame_queue_, calls parse_frame() → Aeron.
///
/// Subclasses implement:
///   - connect_and_subscribe() — open WS, send initial subscribe frames.
///   - read_loop(ws) — receive loop; call push_frame() for each data frame.
///   - parse_frame(payload, recv_ns) — venue parser + md_pub_ publish.
///
/// The reconnect loop in run() calls connect_and_subscribe() + read_loop()
/// in a tight try/catch. Subclasses may override reconnect_delay()
/// (default 1 s).
template <class Pub>
class AdapterBase : public IAdapter {
public:
    /// 512 slots × 16 KiB ≈ 8 MiB per adapter. 16 KiB covers the largest
    /// expected WS frame (Deribit book snapshot at depth=255 is ~15 KiB).
    /// Bump SLOT_BYTES if a venue starts emitting bigger frames.
    static constexpr size_t QUEUE_CAPACITY = 512;
    static constexpr size_t SLOT_BYTES = 16384;
    using FrameQueue = bpt::common::util::SpscQueue<QUEUE_CAPACITY, SLOT_BYTES>;

    AdapterBase(const config::AdapterConfig& cfg,
                std::shared_ptr<Pub> md_pub,
                std::shared_ptr<messaging::api::FundingRatePublisher> funding_pub,
                std::shared_ptr<messaging::api::InstrumentStatsPublisher> stats_pub)
        : cfg_(cfg),
          md_pub_(std::move(md_pub)),
          funding_pub_(std::move(funding_pub)),
          stats_pub_(std::move(stats_pub)),
          ssl_ctx_(boost::asio::ssl::context::tls_client) {
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(boost::asio::ssl::verify_peer);
        // Enforce TLS 1.2 minimum — disable weak protocol versions.
        ssl_ctx_.set_options(boost::asio::ssl::context::no_tlsv1 | boost::asio::ssl::context::no_tlsv1_1);

        // Self-wire the slow-path callbacks to this adapter's own publishers.
        // The service used to inject these from outside; greenfield principle
        // is that the adapter owns everything it produces, so the wiring is
        // local. The decoders still take FundingRateCallback / InstrumentStatsCallback
        // by reference — the indirection lets bpt-tape inject no-op variants.
        on_funding_rate = [this](const messaging::FundingRateUpdate& fr) {
            if (funding_pub_)
                funding_pub_->publish(fr);
        };
        on_instrument_stats = [this](const messaging::InstrumentStatsUpdate& oi) {
            if (stats_pub_)
                stats_pub_->publish(oi);
        };
    }

    ~AdapterBase() override = default;

    /// IAdapter contract; called from the control thread. We do NOT mutate
    /// subs_ here — that's reserved for the publisher thread (single-writer
    /// principle). Instead, queue a SubCmd and let publish_loop drain it.
    void subscribe(uint64_t instrument_id, std::string symbol, uint8_t depth = 0) override {
        sub_cmd_q_.push(SubCmd{SubCmd::Op::Subscribe, instrument_id, std::move(symbol), depth});
    }

    void unsubscribe(uint64_t instrument_id) override {
        sub_cmd_q_.push(SubCmd{SubCmd::Op::Unsubscribe, instrument_id, std::string{}, 0});
    }

    void start() override {
        pub_thread_ = std::thread([this]() { publish_loop(); });
        thread_ = std::thread([this]() { run(); });
    }

    void stop() override {
        stop_flag_.store(true, std::memory_order_relaxed);
        ioc_.stop();
        if (thread_.joinable())
            thread_.join();
        if (pub_thread_.joinable())
            pub_thread_.join();
    }

    void set_topology(const bpt::common::util::Topology& topology) override { topology_ = &topology; }

    [[nodiscard]] uint64_t md_published_count() const noexcept override { return md_pub_->published(); }
    [[nodiscard]] uint64_t validation_drop_count() const noexcept override { return md_pub_->validation_drops(); }
    [[nodiscard]] uint64_t md_backpressure_drop_count() const noexcept override { return md_pub_->drop_count(); }
    [[nodiscard]] bool validation_drop_breaker_tripped() const noexcept override { return md_pub_->breaker_tripped(); }

protected:
    /// \brief Backoff before the next reconnect attempt. Default 1 s.
    virtual std::chrono::milliseconds reconnect_delay() const { return std::chrono::seconds(1); }

    /// \brief Open the WebSocket and send all initial subscribe frames.
    /// \return nullptr if no subscriptions exist yet — run() retries in 100 ms.
    virtual std::unique_ptr<bpt::common::ws::AnyWsStream> connect_and_subscribe() = 0;

    /// \brief Synchronous receive loop.
    ///
    /// Implementations call push_frame() for each data frame and throw on
    /// any fatal error to trigger a reconnect.
    virtual void read_loop(bpt::common::ws::AnyWsStream& ws) = 0;

    /// \brief Publisher-thread callback invoked once per dequeued frame.
    ///
    /// Implementations run the venue parser then call md_pub_ publish methods.
    virtual void parse_frame(std::string_view payload, uint64_t recv_ns) = 0;

    /// \brief Publisher-thread hook to send a runtime subscribe frame to the
    ///        venue. Called from the SubCmd drain *after* subs_ has been
    ///        updated, so the ordering guarantee is:
    ///            1. subs_ knows about (symbol → instrument_id)
    ///            2. Frame goes out to the exchange
    ///            3. Exchange starts streaming BBOs for that symbol
    ///            4. Decoder's find_id(symbol) succeeds (state already in place)
    ///
    /// Default no-op: Binance bakes subscriptions into the URL, so it
    /// reconnects to subscribe at runtime — no protocol-level send.
    /// OKX / HL / Deribit override this to push the venue-specific
    /// subscribe payload via ws_client_.send (which is thread-safe via
    /// its internal mutex).
    virtual void do_send_subscribe_frame(std::string_view /*symbol*/, uint8_t /*depth*/) {}

    /// \brief Publisher-thread hook to send a runtime unsubscribe frame.
    /// Same ordering guarantee as do_send_subscribe_frame.
    virtual void do_send_unsubscribe_frame(std::string_view /*symbol*/) {}

    /// \brief IO-thread seam invoked by the venue ws-client for each application frame.
    ///
    /// Default implementation enqueues onto the SPSC frame queue for the
    /// publisher thread (push_frame). bpt-tape overrides this to tee
    /// the raw bytes into a Tape before enqueueing — keeps the
    /// recording tap out of the main mdgw source.
    virtual void handle_frame(std::string_view payload, uint64_t recv_ns) noexcept { push_frame(payload, recv_ns); }

    /// \brief Push a raw WS frame onto the SPSC queue. IO-thread only.
    ///
    /// Logs a throttled warning when the queue is full or the frame is
    /// oversized — never blocks the receive path.
    void push_frame(std::string_view payload, uint64_t recv_ns) noexcept {
        if (!frame_queue_.try_push(recv_ns, payload)) {
            ++dropped_frames_;
            // Log at most once every 1000 drops to avoid flooding on sustained backpressure.
            if (dropped_frames_ == 1 || dropped_frames_ % 1000 == 0) {
                bpt::common::log::warn("{}: frame queue full or oversized — dropped frames: {}",
                                       this->exchange_name(),
                                       dropped_frames_);
            }
        }
    }

    config::AdapterConfig cfg_;
    std::shared_ptr<Pub> md_pub_;
    /// Slow-path publishers owned by the adapter, not the service.
    /// Each venue gets its own Aeron publication on funding_rate / instrument_stats.
    /// Aeron handles N publications on one stream natively (multi-session).
    std::shared_ptr<messaging::api::FundingRatePublisher> funding_pub_;
    std::shared_ptr<messaging::api::InstrumentStatsPublisher> stats_pub_;
    /// Decoder-facing callbacks. Initialized in the constructor to forward
    /// into funding_pub_ / stats_pub_. Protected (not public) because they're
    /// implementation detail of the adapter, not part of IAdapter's contract.
    messaging::FundingRateCallback on_funding_rate;
    messaging::InstrumentStatsCallback on_instrument_stats;
    /// Optional CPU-affinity topology. Pointer (not reference) because the
    /// base can be constructed before topology is known; set via
    /// set_topology() before start(). nullptr = fall back to the legacy
    /// cfg_.io_thread_cpu TOML knob.
    const bpt::common::util::Topology* topology_{nullptr};

    /// Subscription state — owned by the publisher thread post Phase 2.
    /// Control thread writes via sub_cmd_q_; publisher thread drains and
    /// applies. Reads still take SubscriptionMap's internal shared_mutex
    /// since the IO thread also reads (at reconnect via snapshot()).
    SubscriptionMap subs_;
    SubCmdQueue sub_cmd_q_;
    boost::asio::io_context ioc_;
    boost::asio::ssl::context ssl_ctx_;

    /// Cache-line isolated. publish_loop checks stop_flag_ on every
    /// spin iteration; without this, it could share a line with the
    /// frame_queue_'s producer-side `head_` atomic and cause every
    /// IO-thread push to invalidate the consumer's stop_flag check.
    alignas(64) std::atomic<bool> stop_flag_{false};

    FrameQueue frame_queue_;

private:
    void run() {
        bpt::common::util::set_thread_name(detail::io_thread_name(this->exchange_name()));
        // Pin policy: prefer central Topology role assignment when set;
        // fall back to the legacy per-adapter cfg_.io_thread_cpu knob for
        // configs that haven't migrated. Both unset = unpinned.
        bool pinned_via_topology = false;
        if (topology_)
            pinned_via_topology = bpt::common::util::pin_thread_by_role(*topology_,
                                                                        detail::io_role(this->exchange_name()),
                                                                        this->exchange_name());
        if (!pinned_via_topology)
            bpt::common::util::pin_thread_to_cpu(cfg_.io_thread_cpu, this->exchange_name());
        while (!stop_flag_.load(std::memory_order_relaxed)) {
            try {
                ioc_.restart();
                md_pub_->reset_validator();
                auto ws = connect_and_subscribe();
                if (!ws) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                if (this->on_connect)
                    this->on_connect();
                read_loop(*ws);
            } catch (const std::exception& e) {
                if (!stop_flag_.load(std::memory_order_relaxed)) {
                    if (this->on_disconnect)
                        this->on_disconnect();
                    bpt::common::log::error("{} error: {}, reconnecting in {}ms",
                                            this->exchange_name(),
                                            e.what(),
                                            reconnect_delay().count());
                    std::this_thread::sleep_for(reconnect_delay());
                }
            }
        }
    }

    void publish_loop() {
        bpt::common::util::set_thread_name(detail::pub_thread_name(this->exchange_name()));

        // Adaptive backoff: tight spin with `pause` while the queue is hot,
        // then yield when it stays empty long enough that we're paying real
        // CPU for nothing. Picking the spin budget conservatively (~few µs
        // worth of pause iterations on x86) keeps wake-up latency in the
        // tens of nanoseconds when the IO thread pushes between consumer
        // iterations — std::this_thread::yield() on a pinned/isolated core
        // is a wasted syscall that adds ~µs of context-switch jitter.
        constexpr int kSpinBudget = 1000;
        int empty_iters = 0;
        while (!stop_flag_.load(std::memory_order_relaxed)) {
            // Apply any pending subscribe/unsubscribe commands from the
            // control thread. Single-writer to subs_ — only this thread
            // ever calls subscribe/unsubscribe on the SubscriptionMap.
            // Drains rarely (commands arrive ~once per session), so the
            // overhead on the hot loop is one mutex lock + empty-deque
            // check per iteration. Negligible.
            sub_cmd_q_.drain([this](SubCmd&& c) {
                if (c.op == SubCmd::Op::Subscribe) {
                    const std::string symbol = c.symbol;  // copy before move
                    const uint8_t depth = c.depth;
                    subs_.subscribe(c.instrument_id, std::move(c.symbol), c.depth);
                    // Order matters: state first, then frame. Decoder must be
                    // able to find_id(symbol) before BBOs for it start arriving.
                    do_send_subscribe_frame(symbol, depth);
                } else {
                    // unsubscribe returns the venue symbol (for the frame);
                    // applied BEFORE the frame goes out so the decoder won't
                    // try to find an entry already in the process of leaving.
                    const std::string symbol = subs_.unsubscribe(c.instrument_id);
                    if (!symbol.empty())
                        do_send_unsubscribe_frame(symbol);
                }
            });

            const bool processed = frame_queue_.try_pop(
                [this](uint64_t recv_ns, std::string_view payload) { parse_frame(payload, recv_ns); });
            if (processed) {
                empty_iters = 0;
                continue;
            }
            if (++empty_iters < kSpinBudget) {
#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#elif defined(__aarch64__)
                asm volatile("yield" ::: "memory");
#endif
            } else {
                std::this_thread::yield();
                empty_iters = kSpinBudget;
            }
        }
        // Drain any frames queued between the IO thread stopping and publish_loop waking.
        while (frame_queue_.try_pop(
            [this](uint64_t recv_ns, std::string_view payload) { parse_frame(payload, recv_ns); })) {
        }
    }

    std::thread thread_;
    std::thread pub_thread_;

    uint64_t dropped_frames_{0};
};

}  // namespace bpt::md_gateway::adapter

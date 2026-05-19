#pragma once

// bpt_common/ws/run_loop.h — WebSocket read/send/ping run-loop base.
//
// Handles the boilerplate that every long-lived authenticated WS client
// needs:
//   - Read loop with configurable per-read timeout so a silent server
//     disconnect unblocks via `beast::error::timeout` instead of hanging
//     forever.
//   - Thread-safe send() callable from any thread. Internally, send()
//     posts the write onto the IO thread's io_context so the actual
//     ws_->write() always executes on the single IO thread — the WS
//     stream is a single-writer resource. No mutex needed.
//   - Optional ping heartbeat driven by an asio steady_timer on the
//     IO thread (no separate ping std::thread).
//   - Clean shutdown on stop_flag and on exceptions thrown from any hook.
//
// What subclasses provide:
//   - on_handshake_complete():  send a login/auth message if the
//     exchange requires it. Called exactly once after the WS handshake
//     completes and BEFORE the read loop starts.
//   - on_frame(payload, recv_ns): called for each inbound text/binary
//     frame. recv_ns is WallClock::now_ns() at receive time.
//   - on_tick(): called on every read_timeout expiry — use for periodic
//     bookkeeping (stale-state cleanup, staleness metrics) that would
//     otherwise need a separate timer thread. Default no-op.
//   - ping_config(): return a cadence + payload-factory if the exchange
//     expects application-level pings (OKX, HL). Return nullopt for
//     exchanges with their own heartbeat protocol (Deribit).
//
// read_timeout vs liveness_timeout:
//   - read_timeout: max per-read wait. On expiry the loop fires on_tick
//     and checks stop_flag, then continues. Keep this short (seconds)
//     so the subclass ticks promptly and the service can shut down
//     quickly. A timeout alone does NOT kill the connection.
//   - liveness_timeout: if > 0, throw when no frame has arrived within
//     liveness_timeout — an application-level watchdog for a silently
//     dead connection (TCP half-open, load balancer blackhole). Leave
//     at 0 for exchanges whose own heartbeat protocol already detects
//     this (Deribit set_heartbeat, or WS-level Beast pings).
//
// What's intentionally NOT in here:
//   - Exchange auth payload construction (each exchange is different).
//   - Request/response correlation (Hyperliquid-style) — kept in its own
//     client because it needs concurrent access to the raw stream, which
//     doesn't fit the single-owner-stream pattern this class uses.

#include "bpt_common/logging.h"
#include "bpt_common/util/tsc_clock.h"
#include "bpt_common/ws/ws_connect.h"

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace bpt::common::ws {

struct PingConfig {
    std::chrono::milliseconds interval;
    // Called on each tick to produce the ping payload. A function (not a
    // fixed string) so exchanges that need a sequence number or nonce
    // can generate fresh payloads per ping.
    std::function<std::string()> payload;
};

class RunLoop {
public:
    RunLoop() = default;
    virtual ~RunLoop() = default;

    RunLoop(const RunLoop&) = delete;
    RunLoop& operator=(const RunLoop&) = delete;

    // Run the read + ping loop against the supplied (already-connected)
    // stream. Returns cleanly when stop_flag goes true; throws on any
    // WS error (including liveness timeout) so the caller's outer
    // reconnect loop can catch + retry.
    //
    // connected is set true after on_handshake_complete() returns, and
    // false on exit (normal or exceptional).
    //
    // ioc is the io_context the WebSocket stream was constructed with;
    // RunLoop drives it via run_one() to dispatch async_read + steady_timer
    // callbacks. The previous sync-read implementation didn't need ioc
    // because Beast's expires_after worked on synchronous reads — except
    // it doesn't in the version we vendor (sync ws.read ignores the
    // timeout while traffic is flowing), so on_tick() was unreliable.
    // Async read + a separate steady_timer makes on_tick deterministic
    // again.
    //
    // See the file header for read_timeout vs liveness_timeout
    // semantics. liveness_timeout == 0 disables the watchdog.
    void run(AnyWsStream ws,
             boost::asio::io_context& ioc,
             std::atomic<bool>& stop_flag,
             std::atomic<bool>& connected,
             std::chrono::milliseconds read_timeout = std::chrono::seconds(30),
             std::chrono::milliseconds liveness_timeout = std::chrono::milliseconds(0));

    // Async write. Posts the write onto the IO thread's io_context, so
    // the actual ws_->write happens on the single IO thread (single-
    // writer principle). Returns true if the work was queued; false if
    // run() isn't currently active. Callers do not get synchronous
    // confirmation of the bytes being on the wire — for the runtime-
    // subscribe / ping use cases this is fine.
    //
    // Safe to call from any thread. The posted handler captures msg
    // by value (one heap allocation per send — only happens at sub
    // events, off the hot path) and runs on whichever thread is driving
    // ioc_.run_one() — that's the IO thread.
    bool send(const std::string& msg);

protected:
    virtual void on_handshake_complete() {}
    virtual void on_frame(std::string_view payload, uint64_t recv_ns) = 0;
    virtual void on_tick() {}
    virtual std::optional<PingConfig> ping_config() const { return std::nullopt; }

private:
    // `running_` is the synchronization between send() callers (any thread)
    // and run() (IO thread). Set true at the bottom of run() entry, before
    // exiting run() it's flipped false → posted lambdas re-check this
    // and short-circuit if run() has exited.
    //
    // `ws_` and `ioc_` are accessed by:
    //   - run() entry/exit (IO thread): writes
    //   - posted send() lambdas (IO thread): reads
    //   - send() callers (any thread): reads ioc_ pointer to do the post
    //
    // The send() caller's read of ioc_ is unsynchronized intentionally —
    // it's guarded by the running_.load(acquire). If running_ is true,
    // ioc_ was published with release ordering and is safe to read.
    std::atomic<bool> running_{false};
    boost::asio::io_context* ioc_ = nullptr;
    AnyWsStream* ws_ = nullptr;
};

inline void RunLoop::run(AnyWsStream ws,
                         boost::asio::io_context& ioc,
                         std::atomic<bool>& stop_flag,
                         std::atomic<bool>& connected,
                         std::chrono::milliseconds read_timeout,
                         std::chrono::milliseconds liveness_timeout) {
    namespace beast = boost::beast;

    ws.text(true);
    // Clear any sync-read deadline — async_read drives its own timer.
    ws.expires_never();

    // Publish the IO-thread-owned resources. Order matters:
    //   1. Write pointers (relaxed — only the IO thread reads them now).
    //   2. Release-store running_ = true. send() callers that load
    //      running_ with acquire ordering will then see ioc_/ws_ as set.
    ws_ = &ws;
    ioc_ = &ioc;
    running_.store(true, std::memory_order_release);

    // Clear the published resources on exit (clean or exceptional). Posted
    // send() lambdas check running_ and short-circuit when it's false, so
    // pending work doesn't access freed memory after we return.
    struct ExitGuard {
        RunLoop* self;
        ~ExitGuard() {
            self->running_.store(false, std::memory_order_release);
            self->ws_ = nullptr;
            self->ioc_ = nullptr;
        }
    } exit_guard{this};

    // Run the subclass auth hook while we're already tracking the stream.
    // on_handshake_complete() may call send() to transmit a login message;
    // the posted handler will be the first thing the run_one loop below
    // processes, preserving auth ordering.
    on_handshake_complete();

    connected.store(true, std::memory_order_relaxed);

    // Ping is now a steady_timer driven on the IO thread — no separate
    // std::thread. Single-writer for the WS stream.
    boost::asio::steady_timer ping_timer(ioc);
    std::optional<PingConfig> ping_cfg = ping_config();
    std::function<void()> arm_ping;
    if (ping_cfg) {
        arm_ping = [this, &arm_ping, &ping_timer, &stop_flag, cfg = *ping_cfg]() {
            ping_timer.expires_after(cfg.interval);
            ping_timer.async_wait([this, &arm_ping, &stop_flag, cfg](beast::error_code ec) {
                if (ec)
                    return;  // cancelled (shutdown / exit)
                if (stop_flag.load(std::memory_order_relaxed))
                    return;
                try {
                    if (ws_)
                        ws_->write(boost::asio::buffer(cfg.payload()));
                } catch (...) {
                    // Ping write errors are expected on disconnect — the
                    // read handler will observe the same failure on its
                    // next async_read completion and propagate. Don't
                    // re-arm.
                    return;
                }
                arm_ping();
            });
        };
        arm_ping();
    }

    // Track the wall-clock time of the last inbound frame so the
    // liveness watchdog can fire on a silently-dead connection.
    // Initialised to "now" so a just-opened socket isn't immediately
    // flagged as stale before the first frame arrives.
    uint64_t last_recv_ns = bpt::common::util::WallClock::now_ns();
    const uint64_t liveness_ns = static_cast<uint64_t>(liveness_timeout.count()) * 1'000'000ULL;

    // Permanent-pending async pattern: one async_read sits on the WS,
    // a separate steady_timer drives on_tick at read_timeout cadence.
    // Both re-arm themselves from their handlers. The outer loop just
    // pumps run_one() until something signals exit (stop_flag, ws
    // error, on_frame / on_tick throw, or liveness watchdog).
    //
    // Why not the previous sync-read pattern: Beast's expires_after
    // applies to the underlying TCP stream's timeouts, but in this
    // vendored version it doesn't actually time out a SYNC ws.read()
    // while frames are flowing. on_tick() therefore stopped firing
    // whenever the WS saw any traffic at all (pings, pongs, fast frame
    // bursts), and three adapters had to override subscribe() to push
    // subscribes immediately rather than wait for the next tick. async
    // I/O fixes this for every periodic-tick use uniformly.
    beast::flat_buffer buf;
    beast::error_code stored_ec;
    std::exception_ptr stored_exc;
    boost::asio::steady_timer tick_timer(ioc);

    std::function<void()> arm_read;
    std::function<void()> arm_tick;

    auto trigger_exit = [&]() {
        // Cancel all pending ops so the outer run_one() loop drains
        // and exits. Each cancel races safely with the other handler;
        // whichever fires first sees operation_aborted and returns
        // without re-arming.
        beast::error_code ce;
        tick_timer.cancel();
        ping_timer.cancel();
        ws.lowest_layer_cancel(ce);
    };

    arm_read = [&]() {
        ws.async_read(buf, [&](beast::error_code ec, std::size_t /*n*/) {
            // operation_aborted only happens when we cancelled — exit path.
            if (ec == boost::asio::error::operation_aborted)
                return;
            if (ec) {
                if (!stored_ec && !stored_exc)
                    stored_ec = ec;
                trigger_exit();
                return;
            }
            const uint64_t recv_ns = bpt::common::util::WallClock::now_ns();
            last_recv_ns = recv_ns;
            std::string_view payload(static_cast<const char*>(buf.data().data()), buf.data().size());
            try {
                on_frame(payload, recv_ns);
            } catch (...) {
                stored_exc = std::current_exception();
                trigger_exit();
                return;
            }
            buf.consume(buf.size());
            if (stop_flag.load(std::memory_order_relaxed))
                return;  // shutdown — don't re-arm
            arm_read();
        });
    };

    arm_tick = [&]() {
        tick_timer.expires_after(read_timeout);
        tick_timer.async_wait([&](beast::error_code ec) {
            if (ec)
                return;  // cancelled by trigger_exit / shutdown
            if (stop_flag.load(std::memory_order_relaxed))
                return;

            if (liveness_ns > 0) {
                const uint64_t now_ns = bpt::common::util::WallClock::now_ns();
                if (now_ns - last_recv_ns > liveness_ns) {
                    // Escalate to the outer reconnect loop — a silent
                    // stream is treated the same as an explicit WS error.
                    if (!stored_ec && !stored_exc)
                        stored_ec = beast::error::timeout;
                    trigger_exit();
                    return;
                }
            }

            try {
                on_tick();
            } catch (...) {
                stored_exc = std::current_exception();
                trigger_exit();
                return;
            }
            arm_tick();
        });
    };

    arm_read();
    arm_tick();

    // Drive the io_context until shutdown or an error signal. Using
    // run_one (rather than run) keeps each handler invocation in this
    // thread without nested-handler surprises; the outer loop re-checks
    // exit conditions between handler invocations.
    while (!stop_flag.load(std::memory_order_relaxed) && !stored_ec && !stored_exc) {
        if (ioc.stopped())
            break;
        if (ioc.run_one() == 0)
            break;  // io_context drained — handlers are done re-arming
    }

    // Outer loop exited. Cancel any still-pending op so the io_context
    // can be re-used by the adapter's reconnect path. The handlers will
    // see operation_aborted; we drain them with poll() so they don't
    // fire on a later io_context iteration.
    {
        beast::error_code ce;
        tick_timer.cancel();
        ping_timer.cancel();
        ws.lowest_layer_cancel(ce);
        while (ioc.poll_one() > 0) {
        }
    }

    connected.store(false, std::memory_order_relaxed);

    // Best-effort close. Most error paths leave the socket in a state
    // where close fails — that's fine, the destructor of `ws` will
    // shut down the TCP socket cleanly anyway.
    try {
        ws.close(boost::beast::websocket::close_code::normal);
    } catch (...) {
    }

    if (stored_exc)
        std::rethrow_exception(stored_exc);
    if (stored_ec)
        throw beast::system_error(stored_ec);
}

inline bool RunLoop::send(const std::string& msg) {
    // Acquire-load running_; if true, ioc_ was published with release
    // ordering earlier and is safe to read here.
    if (!running_.load(std::memory_order_acquire))
        return false;
    boost::asio::post(*ioc_, [this, msg]() {
        // Runs on the IO thread (the only thread driving ioc_.run_one).
        // running_ is re-checked here because by the time the handler
        // fires, run() may have exited and cleared ws_/ioc_.
        if (!running_.load(std::memory_order_acquire) || ws_ == nullptr)
            return;
        try {
            ws_->write(boost::asio::buffer(msg));
        } catch (...) {
            // The read loop's async_read will observe the same WS error
            // on its next completion and propagate to the reconnect
            // path. Swallowing here keeps the IO thread alive to drain
            // remaining handlers.
        }
    });
    return true;
}

}  // namespace bpt::common::ws

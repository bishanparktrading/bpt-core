#include "md_gateway/adapter/okx/okx_md_adapter.h"

#include "md_gateway/adapter/okx/okx_md_encoder.h"

#include <boost/asio/buffer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <bpt_common/ws/ws_connect.h>

namespace bpt::md_gateway::adapter {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;

OkxMdAdapter::OkxMdAdapter(const config::AdapterConfig& cfg, std::shared_ptr<messaging::IMdPublisher> md_pub)
    : AdapterBase(cfg, std::move(md_pub)),
      decoder_(subs_),
      ws_client_(cfg_, subs_) {
    ws_client_.set_frame_handler(
        [this](std::string_view p, uint64_t t) { handle_frame(p, t); });
}

std::unique_ptr<bpt::common::ws::AnyWsStream> OkxMdAdapter::connect_and_subscribe() {
    bpt::common::log::info("OkxMdAdapter connecting {}:{}{} (tls={})", cfg_.ws_host, cfg_.ws_port, cfg_.ws_path, cfg_.use_tls);

    std::unique_ptr<bpt::common::ws::AnyWsStream> any;
    if (cfg_.use_tls) {
        auto ws = bpt::common::ws::ws_connect(ioc_,
                                      ssl_ctx_,
                                      cfg_.ws_host,
                                      cfg_.ws_port,
                                      cfg_.ws_path,
                                      cfg_.so_rcvbuf_bytes,
                                      cfg_.ws_connect_timeout_ms,
                                      "bpt-md-gateway/0.1",
                                      cfg_.pinned_tls_sha256);
        any = std::make_unique<bpt::common::ws::AnyWsStream>(std::move(ws));
    } else {
        auto ws = bpt::common::ws::ws_connect_plain(ioc_,
                                            cfg_.ws_host,
                                            cfg_.ws_port,
                                            cfg_.ws_path,
                                            cfg_.so_rcvbuf_bytes,
                                            cfg_.ws_connect_timeout_ms);
        any = std::make_unique<bpt::common::ws::AnyWsStream>(std::move(ws));
    }

    // OKX requires text frames; Beast defaults to binary. Set here (not
    // in RunLoop::run) so initial subscribe writes below use text mode.
    any->text(true);

    // OKX keepalive must use text-frame "ping" messages, not WebSocket control
    // pings — disable Beast's built-in pings to prevent silent disconnects.
    // The ws-client's ping thread (see ping_config) sends the text pings.
    //
    // idle_timeout left at a positive value because setting it to none()
    // appears to also nullify RunLoop's per-read `expires_after(read_timeout)`
    // in this Beast version, causing on_tick to never fire and runtime-added
    // subscriptions to never reach the WS. A long idle (60s) is plenty given
    // we have our own ping thread + liveness watchdog above it.
    any->set_option(websocket::stream_base::timeout{
        websocket::stream_base::none(),           // connect timeout handled in ws_connect
        std::chrono::seconds(60),                 // idle ≥ max tolerable silence before escalation
        false                                     // no Beast keep-alive pings
    });

    bpt::common::log::info("OkxMdAdapter connected, subscribing instruments");

    // Drain pending so the read loop does not re-send what we subscribe here.
    subs_.take_pending();
    for (const auto& [id, entry] : subs_.snapshot())
        any->write(net::buffer(okx::build_subscribe_payload(entry.symbol, entry.depth)));

    return any;
}

void OkxMdAdapter::read_loop(bpt::common::ws::AnyWsStream& ws) {
    // Ownership of ws transfers to the ws-client for the duration of the
    // session. read_timeout controls on_tick cadence + shutdown
    // responsiveness; liveness_timeout escalates a silent connection.
    ws_client_.run(std::move(ws),
                   stop_flag_,
                   rl_connected_,
                   std::chrono::milliseconds(cfg_.ws_read_timeout_ms),
                   std::chrono::milliseconds(cfg_.ws_liveness_timeout_ms));
}

void OkxMdAdapter::subscribe(uint64_t instrument_id, std::string symbol, uint8_t depth) {
    // Push through the base class so subs_ tracks the state (connect-time
    // replay, requeue after disconnect, etc).
    AdapterBase::subscribe(instrument_id, symbol, depth);

    // Flush the new subscription to the OKX WS immediately if we're
    // connected. ws_client_.send returns false when the stream is null
    // (between reconnects); the frame will be picked up by
    // connect_and_subscribe at the next reconnect via subs_.snapshot().
    //
    // This bypasses the on_tick fallback, which in this Beast version
    // can sit in ws.read() indefinitely — ws.expires_after() doesn't
    // time out sync reads, so on_tick only fires when the WS is
    // literally silent for the full read_timeout, which doesn't happen
    // while OKX responds to our ping thread. Runtime subscribes would
    // otherwise never reach the wire.
    if (ws_client_.send(okx::build_subscribe_payload(symbol, depth))) {
        bpt::common::log::info("OkxMdAdapter: runtime subscribe {} depth={}", symbol, depth);
        // Drain pending to avoid on_tick double-sending this entry.
        subs_.take_pending();
    }
}

void OkxMdAdapter::parse_frame(std::string_view payload, uint64_t recv_ns) {
    decoder_.decode(payload, recv_ns, validating_pub_, on_funding_rate);
}

}  // namespace bpt::md_gateway::adapter

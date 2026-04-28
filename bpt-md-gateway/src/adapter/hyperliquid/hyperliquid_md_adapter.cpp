#include "md_gateway/adapter/hyperliquid/hyperliquid_md_adapter.h"

#include "md_gateway/adapter/hyperliquid/hyperliquid_md_encoder.h"

#include <boost/asio/buffer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <bpt_common/ws/ws_connect.h>

namespace bpt::md_gateway::adapter {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;

HyperliquidMdAdapter::HyperliquidMdAdapter(const config::AdapterConfig& cfg,
                                       std::shared_ptr<messaging::IMdPublisher> md_pub)
    : AdapterBase(cfg, std::move(md_pub)),
      decoder_(subs_),
      ws_client_(cfg_, subs_) {
    ws_client_.set_frame_handler(
        [this](std::string_view p, uint64_t t) { handle_frame(p, t); });
}

std::unique_ptr<bpt::common::ws::AnyWsStream> HyperliquidMdAdapter::connect_and_subscribe() {
    bpt::common::log::info("HyperliquidMdAdapter connecting {}:{}{} (tls={})",
                           cfg_.ws_host, cfg_.ws_port, cfg_.ws_path, cfg_.use_tls);
    std::unique_ptr<bpt::common::ws::AnyWsStream> ws;
    if (cfg_.use_tls) {
        auto tls_ws = bpt::common::ws::ws_connect(ioc_,
                                          ssl_ctx_,
                                          cfg_.ws_host,
                                          cfg_.ws_port,
                                          cfg_.ws_path,
                                          cfg_.so_rcvbuf_bytes,
                                          cfg_.ws_connect_timeout_ms,
                                          "bpt-md-gateway/0.1",
                                          cfg_.pinned_tls_sha256);
        ws = std::make_unique<bpt::common::ws::AnyWsStream>(std::move(tls_ws));
    } else {
        auto plain_ws = bpt::common::ws::ws_connect_plain(ioc_,
                                                  cfg_.ws_host,
                                                  cfg_.ws_port,
                                                  cfg_.ws_path,
                                                  cfg_.so_rcvbuf_bytes,
                                                  cfg_.ws_connect_timeout_ms);
        ws = std::make_unique<bpt::common::ws::AnyWsStream>(std::move(plain_ws));
    }

    // Enable WebSocket-level keep-alive pings. If HL stops responding Beast
    // closes the stream with an error, triggering the reconnect loop.
    // Complements the application-level ping via ping_config + the
    // liveness_timeout watchdog inside RunLoop.
    ws->set_option(websocket::stream_base::timeout{
        websocket::stream_base::none(),  // connect timeout handled in ws_connect
        std::chrono::seconds(30),        // idle timeout before Beast sends a ping
        true                             // send keep-alive ping frames
    });

    bpt::common::log::info("HyperliquidMdAdapter connected, subscribing instruments");

    // Drain pending so the read loop does not re-send what we subscribe here.
    subs_.take_pending();
    for (const auto& [id, entry] : subs_.snapshot()) {
        for (const char* type : {"l2Book", "trades", "activeAssetCtx"})
            ws->write(net::buffer(hyperliquid::build_subscribe_payload(type, entry.symbol)));
    }

    return ws;
}

void HyperliquidMdAdapter::read_loop(bpt::common::ws::AnyWsStream& ws) {
    ws_client_.run(std::move(ws),
                   stop_flag_,
                   rl_connected_,
                   std::chrono::milliseconds(cfg_.ws_read_timeout_ms),
                   std::chrono::milliseconds(cfg_.ws_liveness_timeout_ms));
}

void HyperliquidMdAdapter::subscribe(uint64_t instrument_id, std::string symbol, uint8_t depth) {
    AdapterBase::subscribe(instrument_id, symbol, depth);
    // Push to the wire immediately when connected. See OkxMdAdapter::subscribe
    // for the underlying rationale — sync ws.read doesn't time out here, so
    // on_tick is unreliable for runtime subs.
    bool sent = false;
    for (const char* type : {"l2Book", "trades", "activeAssetCtx"}) {
        if (ws_client_.send(hyperliquid::build_subscribe_payload(type, symbol)))
            sent = true;
    }
    if (sent) {
        bpt::common::log::info("HyperliquidMdAdapter: runtime subscribe {}", symbol);
        subs_.take_pending();  // don't double-send in on_tick
    }
}

void HyperliquidMdAdapter::parse_frame(std::string_view payload, uint64_t recv_ns) {
    decoder_.decode(payload, recv_ns, validating_pub_, on_funding_rate);
}

}  // namespace bpt::md_gateway::adapter

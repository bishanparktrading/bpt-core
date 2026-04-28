#include "md_gateway/adapter/deribit/deribit_md_adapter.h"

#include "md_gateway/adapter/deribit/deribit_md_encoder.h"

#include <boost/asio/buffer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <bpt_common/ws/ws_connect.h>

namespace bpt::md_gateway::adapter {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;

DeribitMdAdapter::DeribitMdAdapter(const config::AdapterConfig& cfg, std::shared_ptr<messaging::IMdPublisher> md_pub)
    : AdapterBase(cfg, std::move(md_pub)),
      decoder_(subs_),
      ws_client_(cfg_, subs_) {
    ws_client_.set_frame_handler(
        [this](std::string_view p, uint64_t t) { handle_frame(p, t); });
}

void DeribitMdAdapter::unsubscribe(uint64_t instrument_id) {
    std::string symbol = subs_.unsubscribe(instrument_id);
    if (!symbol.empty())
        decoder_.forget(symbol);
}

std::chrono::milliseconds DeribitMdAdapter::reconnect_delay() const {
    return std::chrono::seconds(2);
}

std::unique_ptr<bpt::common::ws::AnyWsStream> DeribitMdAdapter::connect_and_subscribe() {
    bpt::common::log::info("DeribitMdAdapter connecting {}:{}{}", cfg_.ws_host, cfg_.ws_port, cfg_.ws_path);
    auto tls_ws = bpt::common::ws::ws_connect(ioc_,
                                      ssl_ctx_,
                                      cfg_.ws_host,
                                      cfg_.ws_port,
                                      cfg_.ws_path,
                                      cfg_.so_rcvbuf_bytes,
                                      cfg_.ws_connect_timeout_ms,
                                      "bpt-md-gateway/0.1",
                                      cfg_.pinned_tls_sha256);
    auto ws = std::make_unique<bpt::common::ws::AnyWsStream>(std::move(tls_ws));

    ws->text(true);
    ws->set_option(websocket::stream_base::timeout{
        websocket::stream_base::none(),  // connect timeout handled in ws_connect
        websocket::stream_base::none(),  // no idle timeout — Deribit uses JSON-RPC heartbeat
        false                            // no Beast keep-alive pings
    });

    bpt::common::log::info("DeribitMdAdapter connected");

    // Enable Deribit heartbeat — CRITICAL: Deribit disconnects within 30s if
    // test_request is not answered with public/test.
    ws->write(net::buffer(deribit::build_set_heartbeat_rpc(
        ws_client_.next_rpc_id(), /*interval_s=*/30)));
    bpt::common::log::info("DeribitMdAdapter: heartbeat enabled (interval=30s)");

    // Clear stale order book gap state before receiving new snapshots.
    decoder_.reset();

    // Drain pending so the read loop does not re-send what we subscribe here.
    subs_.take_pending();
    for (const auto& [id, entry] : subs_.snapshot())
        ws->write(net::buffer(deribit::build_subscribe_rpc(
            ws_client_.next_rpc_id(), entry.symbol, entry.depth)));

    return ws;
}

void DeribitMdAdapter::read_loop(bpt::common::ws::AnyWsStream& ws) {
    ws_client_.run(std::move(ws),
                   stop_flag_,
                   rl_connected_,
                   std::chrono::milliseconds(cfg_.ws_read_timeout_ms),
                   std::chrono::milliseconds(cfg_.ws_liveness_timeout_ms));
}

void DeribitMdAdapter::subscribe(uint64_t instrument_id, std::string symbol, uint8_t depth) {
    AdapterBase::subscribe(instrument_id, symbol, depth);
    // Push to the wire immediately when connected. See OkxMdAdapter::subscribe
    // for the underlying rationale.
    if (ws_client_.send(deribit::build_subscribe_rpc(
            ws_client_.next_rpc_id(), symbol, depth))) {
        bpt::common::log::info("DeribitMdAdapter: runtime subscribe {} depth={}", symbol, depth);
        subs_.take_pending();
    }
}

void DeribitMdAdapter::parse_frame(std::string_view payload, uint64_t recv_ns) {
    decoder_.decode(payload, recv_ns, validating_pub_, on_funding_rate);
    if (decoder_.take_test_request())
        ws_client_.signal_test_request();
}

}  // namespace bpt::md_gateway::adapter

#include "huginn/adapter/hyperliquid/hyperliquid_adapter.h"

#include <boost/asio/buffer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <fmt/format.h>
#include <yggdrasil/util/tsc_clock.h>
#include <yggdrasil/ws/ws_connect.h>

namespace huginn::adapter {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;

HyperliquidAdapter::HyperliquidAdapter(const config::AdapterConfig& cfg,
                                       std::shared_ptr<messaging::IMdPublisher> md_pub)
    : AdapterBase(cfg, std::move(md_pub)),
      parser_(subs_) {}

void HyperliquidAdapter::send_instrument_subs(ygg::ws::AnyWsStream& ws, const std::string& coin) {
    for (const char* type : {"l2Book", "trades", "activeAssetCtx"}) {
        auto sub = fmt::format(R"({{"method":"subscribe","subscription":{{"type":"{}","coin":"{}"}}}})", type, coin);
        ws.write(net::buffer(sub));
    }
}

std::unique_ptr<ygg::ws::AnyWsStream> HyperliquidAdapter::connect_and_subscribe() {
    ygg::log::info("HyperliquidAdapter connecting {}:{}{}", cfg_.ws_host, cfg_.ws_port, cfg_.ws_path);
    auto tls_ws = ygg::ws::ws_connect(ioc_,
                                      ssl_ctx_,
                                      cfg_.ws_host,
                                      cfg_.ws_port,
                                      cfg_.ws_path,
                                      cfg_.so_rcvbuf_bytes,
                                      cfg_.ws_connect_timeout_ms);
    auto ws = std::make_unique<ygg::ws::AnyWsStream>(std::move(tls_ws));

    // Enable WebSocket-level keep-alive pings. If HL stops responding Beast
    // closes the stream with an error, triggering the reconnect loop.
    // Complements the application-level last_recv liveness check in read_loop.
    ws->set_option(websocket::stream_base::timeout{
        websocket::stream_base::none(),  // connect timeout handled in ws_connect
        std::chrono::seconds(30),        // idle timeout before Beast sends a ping
        true                             // send keep-alive ping frames
    });

    ygg::log::info("HyperliquidAdapter connected, subscribing instruments");

    // Drain pending so the read loop does not re-send what we're about to subscribe.
    subs_.take_pending();

    for (const auto& [id, entry] : subs_.snapshot())
        send_instrument_subs(*ws, entry.symbol);

    return ws;
}

void HyperliquidAdapter::read_loop(ygg::ws::AnyWsStream& ws) {
    beast::flat_buffer buf;
    const auto liveness = std::chrono::milliseconds(cfg_.ws_liveness_timeout_ms);
    auto last_recv = std::chrono::steady_clock::now();

    while (!stop_flag_.load(std::memory_order_relaxed)) {
        // Reset timer first — covers subscribe frame writes and the read.
        ws.expires_after(std::chrono::milliseconds(cfg_.ws_read_timeout_ms));

        // Send subscribe frames for any instruments added since connect.
        for (const auto& entry : subs_.take_pending()) {
            send_instrument_subs(ws, entry.symbol);
            ygg::log::info("HyperliquidAdapter: runtime subscribe {}", entry.symbol);
        }

        beast::error_code ec;
        ws.read(buf, ec);

        if (ec == beast::error::timeout) {
            buf.consume(buf.size());
            if (std::chrono::steady_clock::now() - last_recv >= liveness) {
                ygg::log::warn("HyperliquidAdapter: no data for {}ms, reconnecting", cfg_.ws_liveness_timeout_ms);
                throw std::runtime_error("liveness timeout");
            }
            continue;
        }
        if (ec)
            throw beast::system_error(ec);

        last_recv = std::chrono::steady_clock::now();
        uint64_t recv_ns = ygg::util::TscClock::now_epoch_ns();
        push_frame(std::string_view(static_cast<const char*>(buf.data().data()), buf.data().size()), recv_ns);
        buf.consume(buf.size());
    }
    ws.close(websocket::close_code::normal);
}

void HyperliquidAdapter::parse_frame(std::string_view payload, uint64_t recv_ns) {
    parser_.parse(payload, recv_ns, validating_pub_, on_funding_rate);
}

}  // namespace huginn::adapter

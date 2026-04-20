#include "md_gateway/adapter/okx/okx_adapter.h"

#include <boost/asio/buffer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <fmt/format.h>
#include <bpt_common/ws/ws_connect.h>

namespace bpt::md_gateway::adapter {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;

OkxAdapter::OkxAdapter(const config::AdapterConfig& cfg, std::shared_ptr<messaging::IMdPublisher> md_pub)
    : AdapterBase(cfg, std::move(md_pub)),
      parser_(subs_) {}

std::string OkxAdapter::build_subscribe_payload(const std::string& symbol, uint8_t depth) const {
    // Channel selection:
    //   depth 0  → bbo-tbt (tick-by-tick BBO)
    //   depth ≤5 → books5  (top-5 levels)
    //   depth >5 → books   (full depth, 400ms push)
    const char* book_channel = (depth == 0) ? "bbo-tbt" : (depth <= 5) ? "books5" : "books";

    // For perpetual swaps, bundle the funding-rate subscribe in the same
    // frame so the IO round-trip cost matches depth/trade subscribes.
    const bool is_swap = symbol.size() > 5 && symbol.substr(symbol.size() - 5) == "-SWAP";
    if (is_swap) {
        return fmt::format(
            R"({{"op":"subscribe","args":[)"
            R"({{"channel":"{}","instId":"{}"}},)"
            R"({{"channel":"trades","instId":"{}"}},)"
            R"({{"channel":"funding-rate","instId":"{}"}}]}})",
            book_channel, symbol, symbol, symbol);
    }
    return fmt::format(
        R"({{"op":"subscribe","args":[)"
        R"({{"channel":"{}","instId":"{}"}},)"
        R"({{"channel":"trades","instId":"{}"}}]}})",
        book_channel, symbol, symbol);
}

std::unique_ptr<bpt::common::ws::AnyWsStream> OkxAdapter::connect_and_subscribe() {
    bpt::common::log::info("OkxAdapter connecting {}:{}{} (tls={})", cfg_.ws_host, cfg_.ws_port, cfg_.ws_path, cfg_.use_tls);

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
    // The RunLoop ping thread (see ping_config) sends the text pings.
    any->set_option(websocket::stream_base::timeout{
        websocket::stream_base::none(),  // connect timeout handled in ws_connect
        websocket::stream_base::none(),  // no idle timeout — managed via ping_config
        false                            // no Beast keep-alive pings
    });

    bpt::common::log::info("OkxAdapter connected, subscribing instruments");

    // Drain pending so the read loop does not re-send what we subscribe here.
    subs_.take_pending();
    for (const auto& [id, entry] : subs_.snapshot())
        any->write(net::buffer(build_subscribe_payload(entry.symbol, entry.depth)));

    return any;
}

void OkxAdapter::read_loop(bpt::common::ws::AnyWsStream& ws) {
    // Ownership of ws transfers to RunLoop for the duration of the session.
    // read_timeout controls on_tick cadence + shutdown responsiveness;
    // liveness_timeout escalates a silent connection to a reconnect.
    RunLoop::run(std::move(ws),
                 stop_flag_,
                 rl_connected_,
                 std::chrono::milliseconds(cfg_.ws_read_timeout_ms),
                 std::chrono::milliseconds(cfg_.ws_liveness_timeout_ms));
}

void OkxAdapter::on_frame(std::string_view payload, uint64_t recv_ns) {
    // OKX's app-level heartbeat is bidirectional: the exchange may send
    // "ping" expecting a "pong" reply, and we send "ping" via ping_config.
    // Don't push keepalive text frames onto the parser queue.
    if (payload == "ping") {
        RunLoop::send("pong");
        return;
    }
    if (payload == "pong")
        return;

    push_frame(payload, recv_ns);
}

void OkxAdapter::on_tick() {
    // Send subscribe frames for any instruments added since connect.
    for (const auto& entry : subs_.take_pending()) {
        if (RunLoop::send(build_subscribe_payload(entry.symbol, entry.depth)))
            bpt::common::log::info("OkxAdapter: runtime subscribe {} depth={}", entry.symbol, entry.depth);
    }
}

std::optional<bpt::common::ws::PingConfig> OkxAdapter::ping_config() const {
    return bpt::common::ws::PingConfig{
        std::chrono::milliseconds(cfg_.ws_ping_interval_ms),
        [] { return std::string("ping"); },
    };
}

void OkxAdapter::parse_frame(std::string_view payload, uint64_t recv_ns) {
    parser_.parse(payload, recv_ns, validating_pub_, on_funding_rate);
}

}  // namespace bpt::md_gateway::adapter

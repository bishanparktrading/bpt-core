#include "heimdall/adapter/okx/okx_ws_client.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <chrono>
#include <stdexcept>
#include <yggdrasil/logging.h>
#include <yggdrasil/util/tsc_clock.h>

namespace heimdall::adapter::okx {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

OKXWsClient::OKXWsClient(net::io_context& ioc,
                         ssl::context& ssl_ctx,
                         const config::AdapterConfig& cfg)
    : ioc_(ioc), ssl_ctx_(ssl_ctx), cfg_(cfg) {}

void OKXWsClient::set_message_handler(MessageHandler h) {
    message_handler_ = std::move(h);
}

void OKXWsClient::set_login_msg_builder(LoginMsgBuilder b) {
    login_msg_builder_ = std::move(b);
}

bool OKXWsClient::send(const std::string& frame) {
    std::lock_guard<std::mutex> lk(send_mu_);
    if (!ws_send_)
        return false;
    ws_send_(frame);
    return true;
}

void OKXWsClient::run(std::atomic<bool>& stop_flag, std::atomic<bool>& connected) {
    ygg::log::info("[Heimdall] OKXWsClient connecting {}:{}{} (tls={})",
                   cfg_.ws_host, cfg_.ws_port, cfg_.ws_path, cfg_.use_tls);

    // Generic WS message loop — works for both TLS and plain-TCP stream types.
    auto run_loop = [&](auto& ws) {
        {
            std::lock_guard<std::mutex> lk(send_mu_);
            ws_send_ = [&ws](const std::string& msg) {
                ws.write(net::buffer(msg));
            };
        }

        if (login_msg_builder_)
            ws.write(net::buffer(login_msg_builder_()));

        connected.store(true, std::memory_order_relaxed);
        ygg::log::info("[Heimdall] OKXWsClient connected, waiting for login");

        auto last_ping = std::chrono::steady_clock::now();
        try {
            beast::flat_buffer buf;
            while (!stop_flag.load(std::memory_order_relaxed)) {
                auto now = std::chrono::steady_clock::now();
                if (now - last_ping >= std::chrono::seconds(15)) {
                    {
                        std::lock_guard<std::mutex> lk(send_mu_);
                        if (ws_send_)
                            ws_send_("ping");
                    }
                    last_ping = now;
                }

                beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(5));
                beast::error_code ec;
                ws.read(buf, ec);

                if (ec == beast::error::timeout) {
                    buf.consume(buf.size());
                    continue;
                }
                if (ec)
                    throw beast::system_error(ec);

                uint64_t recv_ns = ygg::util::WallClock::now_ns();
                std::string payload(static_cast<const char*>(buf.data().data()), buf.data().size());
                buf.consume(buf.size());

                if (payload == "ping") {
                    std::lock_guard<std::mutex> lk(send_mu_);
                    if (ws_send_)
                        ws_send_("pong");
                    continue;
                }
                if (payload == "pong")
                    continue;

                if (message_handler_)
                    message_handler_(payload, recv_ns);
            }
        } catch (...) {
            std::lock_guard<std::mutex> lk(send_mu_);
            ws_send_ = nullptr;
            throw;
        }
        {
            std::lock_guard<std::mutex> lk(send_mu_);
            ws_send_ = nullptr;
        }
        beast::error_code ec;
        ws.close(websocket::close_code::normal, ec);
    };

    tcp::resolver resolver(ioc_);

    if (!cfg_.use_tls) {
        // Plain TCP — used when connecting to a local simulation server (backtest).
        websocket::stream<beast::tcp_stream> ws(ioc_);
        auto results = resolver.resolve(cfg_.ws_host, cfg_.ws_port);
        beast::get_lowest_layer(ws).connect(results);
        ws.text(true);
        ws.set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) { req.set(beast::http::field::user_agent, "heimdall/0.1"); }));
        ws.handshake(cfg_.ws_host, cfg_.ws_path);
        ws.set_option(websocket::stream_base::timeout{std::chrono::seconds(15), websocket::stream_base::none(), false});
        run_loop(ws);
        return;
    }

    // TLS path — production.
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws(ioc_, ssl_ctx_);
    auto results = resolver.resolve(cfg_.ws_host, cfg_.ws_port);
    beast::get_lowest_layer(ws).connect(results);

    if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), cfg_.ws_host.c_str()))
        throw std::runtime_error("SSL_set_tlsext_host_name failed");
    ws.next_layer().handshake(ssl::stream_base::client);

    ws.text(true);
    ws.set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req) { req.set(beast::http::field::user_agent, "heimdall/0.1"); }));
    ws.handshake(cfg_.ws_host, cfg_.ws_path);
    ws.set_option(websocket::stream_base::timeout{std::chrono::seconds(15), websocket::stream_base::none(), false});
    run_loop(ws);
}

}  // namespace heimdall::adapter::okx

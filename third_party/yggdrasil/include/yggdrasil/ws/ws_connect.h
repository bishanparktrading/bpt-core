#pragma once

// yggdrasil/ws_connect.h — TLS WebSocket connect helper (Boost.Beast).
//
// Dependencies: Boost.Beast, Boost.Asio, OpenSSL (provided by the consuming project).
//
// Usage:
//   auto ws = ygg::ws::ws_connect(ioc, ssl_ctx, host, port, path);
//   // ws is ready for read/write; connect-phase deadline has been cleared.

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <memory>
#include <stdexcept>
#include <string>

namespace ygg::ws {

using WsStream = boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;

// Resolve host:port, perform TCP connect, TLS handshake, and WebSocket upgrade to path.
// Throws on any failure. Intended to be called from each adapter's connect_and_subscribe().
//
// so_rcvbuf_bytes:    receive buffer size to set on the socket (0 = OS default).
// connect_timeout_ms: hard deadline for the entire connect sequence — DNS + TCP + TLS + WS
//                     upgrade.  After ws_connect returns, the timer is cleared with
//                     expires_never() so the read_loop can set its own per-iteration deadline.
// user_agent:         value sent in the HTTP Upgrade request's User-Agent header.
inline std::unique_ptr<WsStream> ws_connect(boost::asio::io_context& ioc,
                                            boost::asio::ssl::context& ssl_ctx,
                                            const std::string& host,
                                            const std::string& port,
                                            const std::string& path,
                                            uint32_t so_rcvbuf_bytes = 0,
                                            uint32_t connect_timeout_ms = 30000,
                                            const std::string& user_agent = "bpt-client/0.1") {
    boost::asio::ip::tcp::resolver resolver(ioc);
    auto ws = std::make_unique<WsStream>(ioc, ssl_ctx);

    // Single deadline covers the entire connect sequence: DNS, TCP, TLS, WS upgrade.
    boost::beast::get_lowest_layer(*ws).expires_after(std::chrono::milliseconds(connect_timeout_ms));

    auto results = resolver.resolve(host, port);
    boost::beast::get_lowest_layer(*ws).connect(results);

    // Disable Nagle's algorithm — send frames immediately without waiting to batch.
    boost::beast::get_lowest_layer(*ws).socket().set_option(boost::asio::ip::tcp::no_delay(true));

    // Optionally enlarge the kernel receive buffer to absorb exchange bursts.
    if (so_rcvbuf_bytes > 0) {
        boost::beast::get_lowest_layer(*ws).socket().set_option(
            boost::asio::socket_base::receive_buffer_size(static_cast<int>(so_rcvbuf_bytes)));
    }

    if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str()))
        throw std::runtime_error("SSL_set_tlsext_host_name failed");

    // RFC 2818 hostname verification — checks the cert's CN/SAN matches host.
    ws->next_layer().set_verify_callback(boost::asio::ssl::host_name_verification(host));

    ws->next_layer().handshake(boost::asio::ssl::stream_base::client);

    ws->set_option(
        boost::beast::websocket::stream_base::decorator([user_agent](boost::beast::websocket::request_type& req) {
            req.set(boost::beast::http::field::user_agent, user_agent);
        }));
    ws->handshake(host, path);

    // Clear the connect-phase deadline so the read_loop sets its own per-iteration deadline.
    boost::beast::get_lowest_layer(*ws).expires_never();

    return ws;
}

}  // namespace ygg::ws

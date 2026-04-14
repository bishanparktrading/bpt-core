#include "heimdall/adapter/okx/okx_https_client.h"

#include "heimdall/adapter/okx/okx_auth.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <stdexcept>

namespace heimdall::adapter::okx {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

OKXHttpsClient::OKXHttpsClient(const config::AdapterConfig& cfg,
                                const ExchangeCredentials& creds)
    : cfg_(cfg),
      api_key_(creds.api_key),
      secret_key_(creds.secret_key),
      passphrase_(creds.passphrase) {}

namespace {
// Open one TLS stream to cfg.rest_host:rest_port, send the request,
// read the full response, return the body. Connection is torn down
// on return — no pooling.
std::string do_request(const config::AdapterConfig& cfg,
                       http::request<http::string_body>& req) {
    net::io_context ioc;
    ssl::context ssl_ctx(ssl::context::tls_client);
    ssl_ctx.set_default_verify_paths();
    ssl_ctx.set_verify_mode(ssl::verify_peer);

    tcp::resolver resolver(ioc);
    beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);
    if (!SSL_set_tlsext_host_name(stream.native_handle(), cfg.rest_host.c_str()))
        throw std::runtime_error("SSL_set_tlsext_host_name failed");

    auto results = resolver.resolve(cfg.rest_host, cfg.rest_port);
    beast::get_lowest_layer(stream).connect(results);
    stream.handshake(ssl::stream_base::client);

    http::write(stream, req);
    beast::flat_buffer buf;
    http::response<http::string_body> res;
    http::read(stream, buf, res);
    return res.body();
}
}  // namespace

std::string OKXHttpsClient::get_unsigned(const std::string& path) {
    http::request<http::string_body> req(http::verb::get, path, 11);
    req.set(http::field::host, cfg_.rest_host);
    req.set(http::field::user_agent, "heimdall/0.1");
    if (cfg_.testnet)
        req.set("x-simulated-trading", "1");
    return do_request(cfg_, req);
}

std::string OKXHttpsClient::get_signed(const std::string& path) {
    http::request<http::string_body> req(http::verb::get, path, 11);
    sign_get_request(req, cfg_.rest_host, path, api_key_, secret_key_, passphrase_, cfg_.testnet);
    return do_request(cfg_, req);
}

}  // namespace heimdall::adapter::okx

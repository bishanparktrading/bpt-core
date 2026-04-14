#include "heimdall/adapter/binance/binance_https_client.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <stdexcept>

namespace heimdall::adapter::binance {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

BinanceHttpsClient::BinanceHttpsClient(const config::AdapterConfig& cfg,
                                        const ExchangeCredentials& creds)
    : cfg_(cfg), api_key_(creds.api_key), secret_key_(creds.secret_key) {}

std::string BinanceHttpsClient::request(const std::string& method,
                                         const std::string& path,
                                         const std::string& body,
                                         bool with_api_key) {
    net::io_context ioc;
    ssl::context ssl_ctx(ssl::context::tls_client);
    ssl_ctx.set_default_verify_paths();
    ssl_ctx.set_verify_mode(ssl::verify_peer);

    tcp::resolver resolver(ioc);
    beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);

    if (!SSL_set_tlsext_host_name(stream.native_handle(), cfg_.rest_host.c_str()))
        throw std::runtime_error("SSL_set_tlsext_host_name failed");

    auto results = resolver.resolve(cfg_.rest_host, cfg_.rest_port);
    beast::get_lowest_layer(stream).connect(results);
    stream.handshake(ssl::stream_base::client);

    http::verb verb;
    if (method == "POST")
        verb = http::verb::post;
    else if (method == "PUT")
        verb = http::verb::put;
    else if (method == "DELETE")
        verb = http::verb::delete_;
    else
        verb = http::verb::get;

    http::request<http::string_body> req(verb, path, 11);
    req.set(http::field::host, cfg_.rest_host);
    req.set(http::field::user_agent, "heimdall/0.1");
    req.set(http::field::content_type, "application/x-www-form-urlencoded");
    if (with_api_key)
        req.set("X-MBX-APIKEY", api_key_);
    req.body() = body;
    req.prepare_payload();

    http::write(stream, req);

    beast::flat_buffer buf;
    http::response<http::string_body> res;
    http::read(stream, buf, res);

    beast::error_code ec;
    stream.shutdown(ec);

    return res.body();
}

}  // namespace heimdall::adapter::binance

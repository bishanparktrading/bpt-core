#pragma once

/// \file
/// \brief Synchronous Boost.Beast HTTPS transport shared across order-gateway adapters.
///
/// Opens a fresh TLS connection per request — OKX and Binance call REST
/// infrequently enough that handshake cost is not load-bearing, and
/// pooling would only complicate failure handling.
///
/// Stays deliberately low-level: the caller builds a fully-formed
/// `http::request` (verb, path, headers, body) and this class only
/// owns the TLS machinery. Exchange-specific concerns (signing,
/// API-key headers, testnet flags) live in the per-exchange
/// https_client wrappers that delegate here.
///
/// Hyperliquid's https_client is intentionally NOT migrated — it
/// carries one-retry connection pooling and is shaped around the
/// adapter's post-action pipelining. Keep it separate.

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <string>

namespace bpt::order_gateway::adapter::common {

class BeastHttpsClient {
public:
    BeastHttpsClient(std::string host, std::string port);

    /// \brief Send `req` over a fresh TLS connection to `host_:port_`.
    /// \return The raw response body.
    /// \throws on transport error; HTTP status is left to the caller to interpret.
    [[nodiscard]] std::string send(boost::beast::http::request<boost::beast::http::string_body>& req) const;

    [[nodiscard]] const std::string& host() const { return host_; }
    [[nodiscard]] const std::string& port() const { return port_; }

private:
    const std::string host_;
    const std::string port_;
};

}  // namespace bpt::order_gateway::adapter::common

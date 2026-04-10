#include "muninn/adapter/deribit/deribit_refdata_adapter.h"

#include "muninn/mapping/instrument_mapping_loader.h"
#include "muninn/refdata/types.h"

#include <bifrost_protocol/DeltaUpdateType.h>
#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/InstrumentType.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <cmath>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <stdexcept>
#include <yggdrasil/util/tsc_clock.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
using json = nlohmann::json;

namespace muninn::adapter {

namespace {

uint64_t now_ns() {
    return ygg::util::TscClock::now_epoch_ns();
}

// Map muninn InstrumentType to bifrost InstrumentType
bifrost::protocol::InstrumentType::Value to_bifrost_inst_type(refdata::InstrumentType t) {
    switch (t) {
        case refdata::InstrumentType::SPOT:
            return bifrost::protocol::InstrumentType::SPOT;
        case refdata::InstrumentType::PERP:
            return bifrost::protocol::InstrumentType::PERPETUAL;
        case refdata::InstrumentType::FUTURE:
            return bifrost::protocol::InstrumentType::FUTURE;
        case refdata::InstrumentType::OPTION:
            return bifrost::protocol::InstrumentType::OPTION;
        default:
            return bifrost::protocol::InstrumentType::NULL_VALUE;
    }
}

// Determine instrument type from Deribit kind + settlement_period.
// kind=future with settlement_period=perpetual → PERP
// kind=future otherwise → FUTURE
// kind=option → OPTION
refdata::InstrumentType deribit_to_inst_type(const std::string& kind, const std::string& settlement_period) {
    if (kind == "option")
        return refdata::InstrumentType::OPTION;
    if (kind == "future") {
        if (settlement_period == "perpetual")
            return refdata::InstrumentType::PERP;
        return refdata::InstrumentType::FUTURE;
    }
    if (kind == "spot")
        return refdata::InstrumentType::SPOT;
    return refdata::InstrumentType::UNKNOWN;
}

// Atomic JSON-RPC request id generator.
std::atomic<uint64_t> g_jsonrpc_id{1};

}  // namespace

DeribitRefDataAdapter::DeribitRefDataAdapter(const config::AdapterConfig& cfg,
                                             const ExchangeCredentials& creds,
                                             std::shared_ptr<registry::InstrumentRegistry> registry,
                                             std::shared_ptr<mapping::InstrumentMappingLoader> mapping)
    : cfg_(cfg),
      registry_(std::move(registry)),
      mapping_(std::move(mapping)),
      client_id_(creds.client_id),
      client_secret_(creds.client_secret) {
    if (client_id_.empty() || client_secret_.empty()) {
        spdlog::warn(
            "[DeribitRefData] Deribit credentials not set — "
            "private endpoints (fee tiers) will not be available; "
            "using per-instrument fees from get_instruments instead.");
    }
}

// ---------------------------------------------------------------------------
// JSON-RPC 2.0 HTTP POST helper
// ---------------------------------------------------------------------------
std::string DeribitRefDataAdapter::http_post_jsonrpc(const std::string& host,
                                                     const std::string& port,
                                                     const std::string& method,
                                                     const std::string& params_json,
                                                     bool use_tls,
                                                     const std::string& access_token) const {
    json req_body;
    req_body["jsonrpc"] = "2.0";
    req_body["id"] = g_jsonrpc_id.fetch_add(1, std::memory_order_relaxed);
    req_body["method"] = method;
    req_body["params"] = json::parse(params_json);

    const std::string body_str = req_body.dump();

    net::io_context ioc;

    if (use_tls) {
        ssl::context ssl_ctx(ssl::context::tls_client);
        ssl_ctx.set_default_verify_paths();
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);
        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
            throw beast::system_error(beast::errc::make_error_code(beast::errc::not_connected));

        beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));

        tcp::resolver resolver(ioc);
        auto results = resolver.resolve(host, port);
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);

        http::request<http::string_body> req{http::verb::post, "/api/v2", 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "muninn/1.0");
        req.set(http::field::content_type, "application/json");
        if (!access_token.empty())
            req.set(http::field::authorization, "Bearer " + access_token);
        req.body() = body_str;
        req.prepare_payload();
        http::write(stream, req);

        beast::flat_buffer buf;
        http::response_parser<http::string_body> parser;
        parser.body_limit(64 * 1024 * 1024);  // Deribit options can be large
        http::read(stream, buf, parser);
        auto res = parser.get();
        if (res.result() != http::status::ok)
            throw std::runtime_error("Deribit HTTP " + std::to_string(static_cast<int>(res.result())) + " for " +
                                     method);
        return res.body();
    } else {
        beast::tcp_stream stream(ioc);
        stream.expires_after(std::chrono::seconds(30));

        tcp::resolver resolver(ioc);
        auto results = resolver.resolve(host, port);
        stream.connect(results);

        http::request<http::string_body> req{http::verb::post, "/api/v2", 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "muninn/1.0");
        req.set(http::field::content_type, "application/json");
        if (!access_token.empty())
            req.set(http::field::authorization, "Bearer " + access_token);
        req.body() = body_str;
        req.prepare_payload();
        http::write(stream, req);

        beast::flat_buffer buf;
        http::response_parser<http::string_body> parser;
        parser.body_limit(64 * 1024 * 1024);
        http::read(stream, buf, parser);
        auto res = parser.get();
        if (res.result() != http::status::ok)
            throw std::runtime_error("Deribit HTTP " + std::to_string(static_cast<int>(res.result())) + " for " +
                                     method);
        return res.body();
    }
}

// ---------------------------------------------------------------------------
// Authenticate — returns access_token for private endpoints
// ---------------------------------------------------------------------------
std::string DeribitRefDataAdapter::authenticate(const std::string& host, const std::string& port, bool use_tls) const {
    json params;
    params["grant_type"] = "client_credentials";
    params["client_id"] = client_id_;
    params["client_secret"] = client_secret_;

    auto body = http_post_jsonrpc(host, port, "public/auth", params.dump(), use_tls);
    auto j = json::parse(body);

    if (j.contains("error")) {
        throw std::runtime_error("Deribit auth failed: " + j["error"].value("message", "unknown error"));
    }

    return j["result"].value("access_token", "");
}

// ---------------------------------------------------------------------------
// Instrument parser
// ---------------------------------------------------------------------------
void DeribitRefDataAdapter::parse_instruments(const std::string& body, uint64_t collected_ts) {
    auto j = json::parse(body);

    if (j.contains("error")) {
        spdlog::error("[DeribitRefData] get_instruments error: {}", j["error"].value("message", "unknown"));
        return;
    }

    const auto& result = j["result"];
    if (!result.is_array()) {
        spdlog::error("[DeribitRefData] get_instruments result is not an array");
        return;
    }

    int loaded = 0;
    for (const auto& sym : result) {
        bool is_active = sym.value("is_active", false);
        if (!is_active)
            continue;

        std::string instrument_name = sym.value("instrument_name", "");
        if (instrument_name.empty())
            continue;

        std::string kind = sym.value("kind", "");
        std::string settlement_period = sym.value("settlement_period", "");
        std::string base = sym.value("base_currency", "");
        std::string quote = sym.value("quote_currency", "");

        refdata::InstrumentType itype = deribit_to_inst_type(kind, settlement_period);
        if (itype == refdata::InstrumentType::UNKNOWN)
            continue;

        // Instrument mapping lookup — Deribit symbols are unique per type (BTC-PERPETUAL, BTC-28MAR25, etc.)
        auto cid = mapping_->try_resolve_canonical_id(mapping::EXCHANGE_ID_DERIBIT, instrument_name);
        if (!cid)
            continue;

        refdata::Instrument inst;
        inst.inst_uid = mapping::make_inst_uid(*cid, mapping::EXCHANGE_ID_DERIBIT);
        inst.venue = "DERIBIT";
        inst.venue_symbol = instrument_name;
        inst.display_name = instrument_name;
        inst.base = base;
        inst.quote = quote;
        inst.inst_type = itype;
        inst.status = refdata::InstrumentStatus::ACTIVE;
        inst.version = collected_ts;

        // tick_size, min_trade_amount (lot_size), contract_size (contract_multiplier)
        inst.tick_size = sym.value("tick_size", 0.0);
        inst.lot_size = sym.value("min_trade_amount", 0.0);
        inst.contract_multiplier = sym.value("contract_size", 1.0);

        // Expiry for FUTURE and OPTION types (expiration_timestamp is ms)
        if (itype == refdata::InstrumentType::FUTURE || itype == refdata::InstrumentType::OPTION) {
            uint64_t exp_ms = sym.value("expiration_timestamp", static_cast<uint64_t>(0));
            if (exp_ms > 0)
                inst.expiry_timestamp = exp_ms * 1'000'000ULL;  // ms → ns
        }

        // Strike price for OPTIONS
        if (itype == refdata::InstrumentType::OPTION) {
            double strike = sym.value("strike", 0.0);
            if (strike > 0.0)
                inst.strike_price = strike;
        }

        if (registry_->update_if_changed(inst) && on_instrument_delta)
            on_instrument_delta(inst, bifrost::protocol::DeltaUpdateType::MODIFY, collected_ts);
        ++loaded;

        // Fees come directly from instrument data (maker_commission, taker_commission).
        // These are decimal fractions (e.g. 0.0003 = 3 bps).
        double maker = sym.value("maker_commission", 0.0);
        double taker = sym.value("taker_commission", 0.0);

        refdata::FeeScheduleState fs;
        fs.exchange_id = bifrost::protocol::ExchangeId::DERIBIT;
        fs.instrument_id = inst.inst_uid;
        fs.instrument_type = to_bifrost_inst_type(itype);
        fs.maker_fee_bps = static_cast<int16_t>(std::round(maker * 10000.0));
        fs.taker_fee_bps = static_cast<int16_t>(std::round(taker * 10000.0));
        fs.updated_ts = collected_ts;

        if (on_fee_schedule)
            on_fee_schedule(fs);
    }
    spdlog::info("[DeribitRefData] Loaded {} active instruments from get_instruments", loaded);
}

// ---------------------------------------------------------------------------
// fetchSnapshot — blocking
// ---------------------------------------------------------------------------
void DeribitRefDataAdapter::fetchSnapshot() {
    spdlog::info("[DeribitRefData] Starting snapshot fetch...");

    const std::string host = cfg_.rest_host.empty() ? "test.deribit.com" : cfg_.rest_host;
    const std::string port = cfg_.rest_port;
    const bool tls = cfg_.use_tls;
    const uint64_t ts = now_ns();

    // Deribit requires fetching per-currency x per-kind.
    // Currencies: BTC, ETH (primary). Kinds: future, option.
    const std::vector<std::string> currencies = {"BTC", "ETH"};
    const std::vector<std::string> kinds = {"future", "option"};

    for (const auto& currency : currencies) {
        for (const auto& kind : kinds) {
            try {
                json params;
                params["currency"] = currency;
                params["kind"] = kind;
                params["expired"] = false;

                auto body = http_post_jsonrpc(host, port, "public/get_instruments", params.dump(), tls);
                parse_instruments(body, ts);
            } catch (const std::exception& e) {
                spdlog::error("[DeribitRefData] Failed to fetch {} {} instruments: {}", currency, kind, e.what());
                // Continue — try remaining currency/kind combos.
            }
        }
    }

    ready_.store(true, std::memory_order_release);
    spdlog::info("[DeribitRefData] Snapshot complete. Registry has {} instruments.", registry_->count());
}

// ---------------------------------------------------------------------------
// fetchInstrumentListing — called hourly
// ---------------------------------------------------------------------------
void DeribitRefDataAdapter::fetchInstrumentListing() {
    spdlog::info("[DeribitRefData] Hourly instrument listing refresh...");
    const uint64_t ts = now_ns();
    const std::string host = cfg_.rest_host.empty() ? "test.deribit.com" : cfg_.rest_host;

    const std::vector<std::string> currencies = {"BTC", "ETH"};
    const std::vector<std::string> kinds = {"future", "option"};

    for (const auto& currency : currencies) {
        for (const auto& kind : kinds) {
            try {
                json params;
                params["currency"] = currency;
                params["kind"] = kind;
                params["expired"] = false;

                auto body =
                    http_post_jsonrpc(host, cfg_.rest_port, "public/get_instruments", params.dump(), cfg_.use_tls);
                parse_instruments(body, ts);
            } catch (const std::exception& e) {
                spdlog::error("[DeribitRefData] Hourly {} {} refresh failed: {}", currency, kind, e.what());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
std::vector<std::string> DeribitRefDataAdapter::get_perp_instrument_names() const {
    std::vector<std::string> names;
    registry_->for_each([&](const refdata::Instrument& inst) {
        if (inst.venue == "DERIBIT" && inst.inst_type == refdata::InstrumentType::PERP)
            names.push_back(inst.venue_symbol);
    });
    return names;
}

}  // namespace muninn::adapter

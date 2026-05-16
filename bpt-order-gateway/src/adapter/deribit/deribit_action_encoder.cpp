#include "order_gateway/adapter/deribit/deribit_action_encoder.h"

#include <boost/json.hpp>

#include <cmath>

namespace bpt::order_gateway::adapter::deribit {

namespace json = boost::json;

namespace {
constexpr double kScale = 1e8;

const char* type_str(bpt::messages::OrderType::Value t) {
    using OT = bpt::messages::OrderType;
    if (t == OT::MARKET)
        return "market";
    return "limit";
}

const char* tif_str(bpt::messages::TimeInForce::Value tif) {
    using TIF = bpt::messages::TimeInForce;
    switch (tif) {
        case TIF::IOC:
            return "immediate_or_cancel";
        case TIF::FOK:
            return "fill_or_kill";
        default:
            return "good_til_cancelled";
    }
}

std::string serialise_rpc(std::string_view method, json::object params, uint64_t req_id) {
    json::object msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = req_id;
    msg["method"] = std::string(method);
    msg["params"] = std::move(params);
    return json::serialize(msg);
}
}  // namespace

std::string build_auth_msg(std::string_view client_id, std::string_view client_secret, uint64_t req_id) {
    json::object params;
    params["grant_type"] = "client_credentials";
    params["client_id"] = std::string(client_id);
    params["client_secret"] = std::string(client_secret);
    return serialise_rpc("public/auth", std::move(params), req_id);
}

// Deribit options use price-tiered tick sizes:
//   price <  0.005 → tick 0.0001
//   price >= 0.005 → tick 0.0005
// Refdata captures only the base `tick_size` (0.0001 for the low tier), so
// the OrderManager rounds to 0.0001 — but prices ≥ 0.005 BTC (every quote
// the options-maker emits) must be a multiple of 0.0005 or Deribit rejects
// with `"must conform to tick size"` (2026-05-15 post-mortem). Proper fix
// is refdata-side tick_size_steps support; this is the localised
// workaround that unblocks live quoting today.
//
// Perps use a flat USD tick (e.g. 0.5 for BTC-PERP) which refdata
// captures correctly — so we only adjust for option instrument names
// (suffix -C or -P).
static double tick_for_option_price(double price) {
    return price < 0.005 ? 0.0001 : 0.0005;
}

static bool is_option(const std::string& instrument_name) {
    if (instrument_name.size() < 2)
        return false;
    const char last = instrument_name.back();
    return (last == 'C' || last == 'P')
           && instrument_name[instrument_name.size() - 2] == '-';
}

std::string build_new_order_msg(const OrderSpec& spec, uint64_t req_id) {
    using OT = bpt::messages::OrderType;
    using OS = bpt::messages::OrderSide;

    double price = static_cast<double>(spec.price_e8) / kScale;
    const double amount = static_cast<double>(spec.quantity_e8) / kScale;

    if (spec.order_type != OT::MARKET && is_option(spec.instrument_name)) {
        const double tick = tick_for_option_price(price);
        price = std::round(price / tick) * tick;
    }

    json::object params;
    params["instrument_name"] = spec.instrument_name;
    params["amount"] = amount;
    params["type"] = type_str(spec.order_type);
    params["label"] = spec.label;
    if (spec.order_type != OT::MARKET) {
        params["price"] = price;
        params["time_in_force"] = tif_str(spec.tif);
    }
    const char* method = (spec.side == OS::BUY) ? "private/buy" : "private/sell";
    return serialise_rpc(method, std::move(params), req_id);
}

std::string build_cancel_msg(std::string_view exchange_order_id, uint64_t req_id) {
    json::object params;
    params["order_id"] = std::string(exchange_order_id);
    return serialise_rpc("private/cancel", std::move(params), req_id);
}

std::string build_edit_msg(std::string_view exchange_order_id,
                           int64_t new_price_e8,
                           uint64_t new_quantity_e8,
                           uint64_t req_id) {
    json::object params;
    params["order_id"] = std::string(exchange_order_id);
    params["amount"] = static_cast<double>(new_quantity_e8) / kScale;
    params["price"] = static_cast<double>(new_price_e8) / kScale;
    return serialise_rpc("private/edit", std::move(params), req_id);
}

std::string build_test_response(uint64_t req_id) {
    return serialise_rpc("public/test", json::object{}, req_id);
}

std::string build_simple_rpc(std::string_view method, const std::string& params_json, uint64_t req_id) {
    json::object params;
    if (!params_json.empty()) {
        auto parsed = json::parse(params_json);
        if (parsed.is_object())
            params = parsed.as_object();
    }
    return serialise_rpc(method, std::move(params), req_id);
}

}  // namespace bpt::order_gateway::adapter::deribit

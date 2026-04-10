#include "heimdall/adapter/hyperliquid/hyperliquid_exec_parser.h"

#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/ExecStatus.h>
#include <bifrost_protocol/FeeCurrency.h>
#include <bifrost_protocol/OrderSide.h>
#include <bifrost_protocol/OrderType.h>
#include <bifrost_protocol/RejectReason.h>
#include <spdlog/spdlog.h>

#include <boost/json.hpp>
#include <string>

namespace heimdall::adapter {

namespace json = boost::json;

static constexpr double kScale = 1e8;

void HyperliquidExecParser::handle_fills(const json::array& fills, uint64_t recv_ns) {
    for (const auto& fill_val : fills) {
        const auto& fill = fill_val.as_object();

        ExecEvent ev{};
        ev.exchange_id = bifrost::protocol::ExchangeId::HYPERLIQUID;
        ev.local_ts_ns = recv_ns;

        // Hyperliquid encodes our internal order_id as the cloid.
        auto cloid_it = fill.find("cloid");
        if (cloid_it != fill.end()) {
            try {
                ev.order_id = std::stoull(std::string(cloid_it->value().as_string()));
            } catch (...) {
                ev.order_id = 0;
            }
        }
        if (ev.order_id == 0) continue;

        ev.exchange_order_id = static_cast<uint64_t>(fill.at("oid").as_int64());
        ev.instrument_id = 0;

        std::string side_str = std::string(fill.at("side").as_string());
        ev.side = (side_str == "B") ? bifrost::protocol::OrderSide::BUY
                                    : bifrost::protocol::OrderSide::SELL;
        ev.order_type = bifrost::protocol::OrderType::LIMIT;

        ev.price = static_cast<int64_t>(std::stod(std::string(fill.at("px").as_string())) * kScale);
        ev.filled_qty =
            static_cast<uint64_t>(std::stod(std::string(fill.at("sz").as_string())) * kScale);
        ev.remaining_qty = 0;  // HL does not provide remaining_qty in fill events

        ev.fee = static_cast<int64_t>(std::stod(std::string(fill.at("fee").as_string())) * kScale);
        ev.fee_currency = bifrost::protocol::FeeCurrency::USDT;
        ev.reject_reason = bifrost::protocol::RejectReason::OK;
        ev.status = bifrost::protocol::ExecStatus::FILLED;

        if (auto ts_it = fill.find("time"); ts_it != fill.end())
            ev.exchange_ts_ns = static_cast<uint64_t>(ts_it->value().as_int64()) * 1000000ULL;
        else
            ev.exchange_ts_ns = recv_ns;

        if (on_exec_event) on_exec_event(ev);
    }
}

}  // namespace heimdall::adapter

#include "order_gateway/adapter/binance/binance_order_adapter.h"

#include "order_gateway/adapter/binance/binance_action_encoder.h"
#include "order_gateway/adapter/binance/binance_auth.h"
#include "order_gateway/adapter/common/credentials.h"

#include <messages/ExchangeId.h>
#include <messages/ExecStatus.h>
#include <messages/OrderSide.h>
#include <messages/OrderType.h>
#include <messages/RejectReason.h>

#include <boost/json.hpp>
#include <bpt_common/util/tsc_clock.h>
#include <chrono>
#include <string>

namespace bpt::order_gateway::adapter {

namespace json = boost::json;

BinanceOrderAdapter::BinanceOrderAdapter(const config::AdapterConfig& cfg, const ExchangeCredentials& creds)
    : OrderAdapterBase(cfg),
      api_key_(creds.api_key),
      secret_key_(creds.secret_key),
      https_client_(cfg_, creds),
      user_data_ws_(ioc_, ssl_ctx_, cfg_, https_client_) {
    decoder_.on_ws_exec_event = [this](const ExecEvent& ev) {
        if (!exec_queue_.try_push(ev))
            bpt::common::log::error("[Binance] exec_queue full — dropped WS ExecEvent order_id={}", ev.order_id);
    };
    decoder_.on_rest_exec_event = [this](const ExecEvent& ev) {
        if (!rest_exec_queue_.try_push(ev))
            bpt::common::log::error("[Binance] rest_exec_queue full — dropped REST ExecEvent order_id={}", ev.order_id);
    };
    user_data_ws_.set_message_handler(
        [this](const std::string& payload, uint64_t recv_ns) { handle_user_data_message(payload, recv_ns); });
}

void BinanceOrderAdapter::handle_user_data_message(const std::string& payload, uint64_t recv_ns) {
    auto root = json::parse(payload);
    if (!root.is_object())
        return;
    decoder_.handle_execution_report(root.as_object(), recv_ns);
}

void BinanceOrderAdapter::connect_and_run() {
    user_data_ws_.run(stop_flag_, connected_);
}

void BinanceOrderAdapter::do_send_new_order_blocking(const util::NewOrderRequest& req) {
    const std::string cloid = "G" + std::to_string(req.order_id);
    decoder_.register_order(cloid, req.order_id);

    const binance::OrderSpec spec{
        req.exchange_symbol,
        req.side,
        req.order_type,
        req.tif,
        req.price,
        req.quantity,
        cloid,
        req.exec_inst,
    };
    const std::string params = binance::build_new_order_params(spec);
    const std::string signed_params = binance::sign_query(secret_key_, params);

    auto emit_rejection = [&]() {
        const uint64_t ts = bpt::common::util::WallClock::now_ns();
        ExecEvent rej;
        rej.order_id = req.order_id;
        rej.exchange_id = bpt::messages::ExchangeId::BINANCE;
        rej.instrument_id = req.instrument_id;
        rej.local_ts_ns = ts;
        rej.exchange_ts_ns = ts;
        rej.side = req.side;
        rej.order_type = req.order_type;
        rej.status = bpt::messages::ExecStatus::REJECTED;
        rej.reject_reason = bpt::messages::RejectReason::EXCHANGE_ERROR;
        if (!rest_exec_queue_.try_push(rej))
            bpt::common::log::error("[Binance] rest_exec_queue full — dropped rejection order_id={}", rej.order_id);
    };

    try {
        const std::string resp = https_client_.request("POST", "/api/v3/order?" + signed_params, "", true);
        bpt::common::log::debug("BinanceOrderAdapter: new order resp (bytes={})", resp.size());

        const uint64_t recv_ns = bpt::common::util::WallClock::now_ns();
        auto root = json::parse(resp);
        if (!root.is_object())
            return;
        decoder_.handle_order_response(root.as_object(), req.order_id, req.side, req.order_type, recv_ns);
    } catch (const std::exception& e) {
        bpt::common::log::error("BinanceOrderAdapter: send_new_order failed: {}", e.what());
        emit_rejection();
    }
}

void BinanceOrderAdapter::do_send_cancel_blocking(const util::CancelRequest& req) {
    const std::string cloid = "G" + std::to_string(req.order_id);
    const std::string params = binance::build_cancel_params(req.native_symbol, cloid);
    const std::string signed_params = binance::sign_query(secret_key_, params);

    try {
        const std::string resp = https_client_.request("DELETE", "/api/v3/order?" + signed_params, "", true);
        bpt::common::log::debug("BinanceOrderAdapter: cancel resp (bytes={})", resp.size());
    } catch (const std::exception& e) {
        bpt::common::log::error("BinanceOrderAdapter: send_cancel failed: {}", e.what());
    }
}

void BinanceOrderAdapter::do_send_cancel_all_blocking(const util::CancelAllRequest& req) {
    bpt::common::log::warn(
        "BinanceOrderAdapter: send_cancel_all called with "
        "instrument_id={}",
        req.instrument_id);
}

void BinanceOrderAdapter::do_send_modify_blocking(const util::ModifyRequest& req) {
    // Binance has no native amend — cancel + replace.
    const std::string cloid = "G" + std::to_string(req.order_id);
    const std::string new_cloid = cloid + "m";

    const std::string cancel_params = binance::build_cancel_params(req.native_symbol, cloid);
    const std::string signed_cancel = binance::sign_query(secret_key_, cancel_params);

    const std::string new_params =
        binance::build_modify_replace_params(req.native_symbol, new_cloid, req.new_price, req.new_quantity);
    const std::string signed_new = binance::sign_query(secret_key_, new_params);

    try {
        https_client_.request("DELETE", "/api/v3/order?" + signed_cancel, "", true);
        https_client_.request("POST", "/api/v3/order?" + signed_new, "", true);
    } catch (const std::exception& e) {
        bpt::common::log::error("BinanceOrderAdapter: send_modify failed: {}", e.what());
    }
}

AccountSnapshotData BinanceOrderAdapter::fetch_account_snapshot(uint64_t correlation_id) {
    const uint64_t ts_ns = bpt::common::util::WallClock::now_ns();
    const uint64_t ts_ms = bpt::common::util::WallClock::now_ms();

    // GET /fapi/v2/account — futures/perp account balance and open positions.
    const std::string params = "timestamp=" + std::to_string(ts_ms);
    const std::string signed_params = binance::sign_query(secret_key_, params);
    const std::string resp = https_client_.request("GET", "/fapi/v2/account?" + signed_params, "", true);

    AccountSnapshotData snap;
    snap.exchange_id = bpt::messages::ExchangeId::BINANCE;
    snap.correlation_id = correlation_id;
    snap.timestamp_ns = ts_ns;

    try {
        auto root = json::parse(resp);
        if (!root.is_object())
            return snap;
        const auto& obj = root.as_object();

        if (obj.contains("availableBalance"))
            snap.available_balance_e8 =
                static_cast<int64_t>(std::round(std::stod(std::string(obj.at("availableBalance").as_string())) * 1e8));
        if (obj.contains("totalWalletBalance"))
            snap.total_equity_e8 = static_cast<int64_t>(
                std::round(std::stod(std::string(obj.at("totalWalletBalance").as_string())) * 1e8));
        if (obj.contains("totalUnrealizedProfit"))
            snap.total_equity_e8 += static_cast<int64_t>(
                std::round(std::stod(std::string(obj.at("totalUnrealizedProfit").as_string())) * 1e8));

        if (obj.contains("positions") && obj.at("positions").is_array()) {
            for (const auto& p : obj.at("positions").as_array()) {
                if (!p.is_object())
                    continue;
                const auto& po = p.as_object();
                const double pos_amt = std::stod(std::string(po.at("positionAmt").as_string()));
                if (pos_amt == 0.0)
                    continue;

                AccountPosition ap;
                ap.exchange_symbol = std::string(po.at("symbol").as_string());
                ap.net_qty_e8 = static_cast<int64_t>(std::round(pos_amt * 1e8));
                if (po.contains("entryPrice"))
                    ap.avg_entry_price_e8 =
                        static_cast<int64_t>(std::round(std::stod(std::string(po.at("entryPrice").as_string())) * 1e8));
                if (po.contains("unrealizedProfit"))
                    ap.unrealized_pnl_e8 = static_cast<int64_t>(
                        std::round(std::stod(std::string(po.at("unrealizedProfit").as_string())) * 1e8));
                snap.positions.push_back(std::move(ap));
            }
        }
    } catch (const std::exception& e) {
        bpt::common::log::error("BinanceOrderAdapter: failed to parse account snapshot: {}", e.what());
    }

    bpt::common::log::info("BinanceOrderAdapter: account snapshot fetched — balance={:.2f} positions={}",
                           static_cast<double>(snap.available_balance_e8) / 1e8,
                           snap.positions.size());
    return snap;
}

}  // namespace bpt::order_gateway::adapter

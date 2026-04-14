#include "heimdall/adapter/okx/okx_order_adapter.h"

#include "heimdall/adapter/common/credentials.h"
#include "heimdall/adapter/okx/okx_action_codec.h"
#include "heimdall/adapter/okx/okx_auth.h"

#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/ExecStatus.h>
#include <bifrost_protocol/FeeCurrency.h>
#include <bifrost_protocol/OrderSide.h>
#include <bifrost_protocol/OrderType.h>
#include <bifrost_protocol/RejectReason.h>

#include <boost/json.hpp>
#include <chrono>
#include <string>

namespace heimdall::adapter {

namespace json = boost::json;

OKXOrderAdapter::OKXOrderAdapter(const config::AdapterConfig& cfg, const ExchangeCredentials& creds)
    : OrderAdapterBase(cfg),
      api_key_(creds.api_key),
      secret_key_(creds.secret_key),
      passphrase_(creds.passphrase),
      https_client_(cfg_, creds),
      ws_client_(ioc_, ssl_ctx_, cfg_) {
    uint32_t epoch_s = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08x", epoch_s);
    session_prefix_ = std::string(buf, 8);

    parser_.on_exec_event = [this](const ExecEvent& ev) {
        if (!exec_queue_.try_push(ev))
            ygg::log::error("[OKX] exec_queue full — dropped ExecEvent order_id={}", ev.order_id);
    };

    ws_client_.set_login_msg_builder(
        [this] { return okx::build_login_msg(api_key_, secret_key_, passphrase_); });
    ws_client_.set_message_handler(
        [this](const std::string& payload, uint64_t recv_ns) { handle_message(payload, recv_ns); });
}

void OKXOrderAdapter::fetch_inst_id_codes() {
    // Fetch instIdCodes for all instrument types we might trade.
    const std::vector<std::string> inst_types = {"SPOT", "SWAP", "FUTURES", "MARGIN"};
    for (const auto& inst_type : inst_types) {
        try {
            std::string resp = https_client_.get_unsigned("/api/v5/public/instruments?instType=" + inst_type);
            auto root = json::parse(resp);
            if (!root.is_object())
                continue;
            auto data_it = root.as_object().find("data");
            if (data_it == root.as_object().end() || !data_it->value().is_array())
                continue;
            bool is_contract_type = inst_type == "SWAP" || inst_type == "FUTURES" || inst_type == "OPTION";
            for (const auto& item : data_it->value().as_array()) {
                const auto& d = item.as_object();
                auto id_it = d.find("instId");
                auto code_it = d.find("instIdCode");
                if (id_it != d.end() && code_it != d.end()) {
                    std::string inst_id = std::string(id_it->value().as_string());
                    int64_t code = code_it->value().is_int64() ? code_it->value().as_int64()
                                                               : std::stoll(std::string(code_it->value().as_string()));
                    inst_id_codes_[inst_id] = code;

                    // ctVal: base currency per contract for SWAP/FUTURES.
                    // SPOT/MARGIN: sz is in base currency, treat as ctVal=1.
                    double ctval = 1.0;
                    if (is_contract_type) {
                        auto ctval_it = d.find("ctVal");
                        if (ctval_it != d.end() && ctval_it->value().is_string()) {
                            std::string sv = std::string(ctval_it->value().as_string());
                            if (!sv.empty())
                                ctval = std::stod(sv);
                        }
                    }
                    contract_sizes_[inst_id] = ctval;
                }
            }
        } catch (const std::exception& e) {
            ygg::log::warn("[Heimdall] OKXOrderAdapter: fetch_inst_id_codes({}) failed: {}", inst_type, e.what());
        }
    }
    ygg::log::info("[Heimdall] OKXOrderAdapter: loaded {} instIdCodes, {} contract sizes from REST",
                   inst_id_codes_.size(),
                   contract_sizes_.size());
    parser_.set_contract_sizes(contract_sizes_);
}

void OKXOrderAdapter::start() {
    // instIdCodes are only available from the real OKX REST API — skip in backtest
    // (use_tls=false means we're talking to a local simulation server).
    if (cfg_.use_tls) {
        fetch_inst_id_codes();
        fetch_and_log_account_config();
    }
    OrderAdapterBase::start();
}

void OKXOrderAdapter::handle_message(const std::string& payload, uint64_t recv_ns) {
    ygg::log::info("[Heimdall] OKXOrderAdapter WS rx: {}", payload.substr(0, 500));
    auto root = json::parse(payload);
    if (!root.is_object())
        return;
    const auto& obj = root.as_object();

    // Event messages: login, subscribe acks, errors
    if (auto eit = obj.find("event"); eit != obj.end()) {
        std::string event = std::string(eit->value().as_string());
        if (event == "error") {
            std::string code, msg;
            if (auto cit = obj.find("code"); cit != obj.end())
                code = std::string(cit->value().as_string());
            if (auto mit = obj.find("msg"); mit != obj.end())
                msg = std::string(mit->value().as_string());
            ygg::log::error("[Heimdall] OKXOrderAdapter: error event code={} msg={}", code, msg);
        } else {
            ygg::log::info("[Heimdall] OKXOrderAdapter: event={}", event);
        }
        if (event == "login") {
            ygg::log::info("[Heimdall] OKXOrderAdapter: login successful");
            logged_in_.store(true, std::memory_order_release);
            json::object sub_msg;
            sub_msg["op"] = "subscribe";
            json::array args;
            json::object arg;
            arg["channel"] = "orders";
            arg["instType"] = "ANY";
            args.push_back(arg);
            sub_msg["args"] = std::move(args);
            ws_client_.send(json::serialize(sub_msg));
        }
        return;
    }

    // Op responses: order-placement acks {"op":"order","data":[...]}
    if (auto op_it = obj.find("op"); op_it != obj.end()) {
        if (std::string(op_it->value().as_string()) == "order") {
            auto data_it = obj.find("data");
            if (data_it == obj.end() || !data_it->value().is_array())
                return;
            for (const auto& item : data_it->value().as_array())
                parser_.handle_order_ack(item.as_object(), recv_ns);
        }
        return;
    }

    // Channel push: orders channel fills/state changes
    auto arg_it = obj.find("arg");
    auto data_it = obj.find("data");
    if (arg_it == obj.end() || data_it == obj.end())
        return;
    if (!data_it->value().is_array() || data_it->value().as_array().empty())
        return;

    std::string channel = std::string(arg_it->value().as_object().at("channel").as_string());
    if (channel == "orders") {
        for (const auto& item : data_it->value().as_array())
            parser_.handle_orders_channel_item(item.as_object(), recv_ns);
    }
}

void OKXOrderAdapter::connect_and_run() {
    logged_in_.store(false, std::memory_order_relaxed);
    parser_.reset();
    ws_client_.run(stop_flag_, connected_);
}

void OKXOrderAdapter::send_new_order(const bifrost::protocol::NewOrder& order) {
    const std::string exchange_symbol = order.getExchangeSymbolAsString();
    const std::string cloid = session_prefix_ + "G" + std::to_string(order.orderId());
    parser_.register_order(cloid, order.orderId());

    const okx::OrderSpec spec{
        exchange_symbol,
        order.side(),
        order.orderType(),
        order.timeInForce(),
        order.price(),
        order.quantity(),
        cloid,
    };
    const uint64_t req_id = ws_req_id_.fetch_add(1, std::memory_order_relaxed);
    const std::string frame = json::serialize(
        okx::build_order_action(spec, req_id, inst_id_codes_, contract_sizes_));
    auto emit_rejection = [&]() {
        uint64_t ts = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        ExecEvent rej;
        rej.order_id = order.orderId();
        rej.exchange_id = bifrost::protocol::ExchangeId::OKX;
        rej.instrument_id = 0;
        rej.local_ts_ns = ts;
        rej.exchange_ts_ns = ts;
        rej.side = (order.side() == bifrost::protocol::OrderSide::BUY) ? bifrost::protocol::OrderSide::BUY
                                                                       : bifrost::protocol::OrderSide::SELL;
        rej.order_type = order.orderType();
        rej.status = bifrost::protocol::ExecStatus::REJECTED;
        rej.reject_reason = bifrost::protocol::RejectReason::EXCHANGE_ERROR;
        if (!exec_queue_.try_push(rej))
            ygg::log::error("[OKX] exec_queue full — dropped rejection order_id={}", rej.order_id);
    };

    try {
        if (!ws_client_.send(frame)) {
            ygg::log::warn(
                "[Heimdall] OKXOrderAdapter: send_new_order: WS not connected, "
                "rejecting order={}",
                order.orderId());
            emit_rejection();
        }
    } catch (const std::exception& e) {
        ygg::log::error("[Heimdall] OKXOrderAdapter: send_new_order failed: {}", e.what());
        emit_rejection();
    }
}

void OKXOrderAdapter::send_cancel(const bifrost::protocol::CancelOrder& cancel, const std::string& native_symbol) {
    const std::string cloid = session_prefix_ + "G" + std::to_string(cancel.orderId());
    const uint64_t req_id = ws_req_id_.fetch_add(1, std::memory_order_relaxed);
    const std::string frame = json::serialize(
        okx::build_cancel_action(native_symbol, cloid, req_id));

    try {
        ws_client_.send(frame);
    } catch (const std::exception& e) {
        ygg::log::error("[Heimdall] OKXOrderAdapter: send_cancel failed: {}", e.what());
    }
}

void OKXOrderAdapter::send_cancel_all(uint64_t instrument_id) {
    ygg::log::warn("[Heimdall] OKXOrderAdapter: send_cancel_all called instrument_id={}", instrument_id);
}

void OKXOrderAdapter::send_modify(const bifrost::protocol::ModifyOrder& modify, const std::string& native_symbol) {
    const std::string cloid = session_prefix_ + "G" + std::to_string(modify.orderId());
    const std::string frame = json::serialize(
        okx::build_modify_action(native_symbol, cloid, modify.newPrice(), modify.newQuantity(), contract_sizes_));

    try {
        ws_client_.send(frame);
    } catch (const std::exception& e) {
        ygg::log::error("[Heimdall] OKXOrderAdapter: send_modify failed: {}", e.what());
    }
}

AccountSnapshotData OKXOrderAdapter::fetch_account_snapshot(uint64_t correlation_id) {
    const uint64_t ts_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());

    AccountSnapshotData snap;
    snap.exchange_id = bifrost::protocol::ExchangeId::OKX;
    snap.correlation_id = correlation_id;
    snap.timestamp_ns = ts_ns;

    try {
        // Balance endpoint — totalEq and availBal.
        auto bal_resp = https_client_.get_signed("/api/v5/account/balance");
        auto bal_j = json::parse(bal_resp);
        if (bal_j.is_object() && bal_j.as_object().contains("data") && bal_j.as_object().at("data").is_array()) {
            const auto& data = bal_j.as_object().at("data").as_array();
            if (!data.empty() && data[0].is_object()) {
                const auto& d = data[0].as_object();
                if (d.contains("totalEq"))
                    snap.total_equity_e8 =
                        static_cast<int64_t>(std::round(std::stod(std::string(d.at("totalEq").as_string())) * 1e8));
                // Sum availBal across USDT details.
                if (d.contains("details") && d.at("details").is_array()) {
                    for (const auto& detail : d.at("details").as_array()) {
                        if (!detail.is_object())
                            continue;
                        const auto& de = detail.as_object();
                        if (de.contains("ccy") && std::string(de.at("ccy").as_string()) == "USDT" &&
                            de.contains("availBal"))
                            snap.available_balance_e8 = static_cast<int64_t>(
                                std::round(std::stod(std::string(de.at("availBal").as_string())) * 1e8));
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        ygg::log::warn("[Heimdall] OKXOrderAdapter: failed to fetch balance: {}", e.what());
    }

    try {
        // Positions endpoint.
        auto pos_resp = https_client_.get_signed("/api/v5/account/positions");
        auto pos_j = json::parse(pos_resp);
        if (pos_j.is_object() && pos_j.as_object().contains("data") && pos_j.as_object().at("data").is_array()) {
            for (const auto& p : pos_j.as_object().at("data").as_array()) {
                if (!p.is_object())
                    continue;
                const auto& po = p.as_object();
                if (!po.contains("pos"))
                    continue;
                const double pos_qty = std::stod(std::string(po.at("pos").as_string()));
                if (pos_qty == 0.0)
                    continue;

                AccountPosition ap;
                if (po.contains("instId"))
                    ap.exchange_symbol = std::string(po.at("instId").as_string());
                ap.net_qty_e8 = static_cast<int64_t>(std::round(pos_qty * 1e8));
                if (po.contains("avgPx") && !po.at("avgPx").as_string().empty())
                    ap.avg_entry_price_e8 =
                        static_cast<int64_t>(std::round(std::stod(std::string(po.at("avgPx").as_string())) * 1e8));
                if (po.contains("upl") && !po.at("upl").as_string().empty())
                    ap.unrealized_pnl_e8 =
                        static_cast<int64_t>(std::round(std::stod(std::string(po.at("upl").as_string())) * 1e8));
                snap.positions.push_back(std::move(ap));
            }
        }
    } catch (const std::exception& e) {
        ygg::log::warn("[Heimdall] OKXOrderAdapter: failed to fetch positions: {}", e.what());
    }

    ygg::log::info("[Heimdall] OKXOrderAdapter: account snapshot fetched — balance={:.2f} positions={}",
                   static_cast<double>(snap.available_balance_e8) / 1e8,
                   snap.positions.size());
    return snap;
}

void OKXOrderAdapter::fetch_and_log_account_config() {
    try {
        auto body = https_client_.get_signed("/api/v5/account/config");
        auto j = json::parse(body);
        if (!j.is_object()) {
            ygg::log::warn("[OKX account/config] unexpected response shape");
            return;
        }
        auto& obj = j.as_object();
        if (obj.contains("code") && obj.at("code").as_string() != "0") {
            ygg::log::warn("[OKX account/config] error code={} msg={}",
                           std::string(obj.at("code").as_string()),
                           obj.contains("msg") ? std::string(obj.at("msg").as_string()) : "");
            return;
        }
        if (!obj.contains("data") || !obj.at("data").is_array() || obj.at("data").as_array().empty())
            return;

        const auto& d = obj.at("data").as_array().at(0).as_object();
        const std::string acct_lv = d.contains("acctLv") ? std::string(d.at("acctLv").as_string()) : "?";
        const std::string perm = d.contains("perm") ? std::string(d.at("perm").as_string()) : "?";
        const std::string pos_mode = d.contains("posMode") ? std::string(d.at("posMode").as_string()) : "?";
        const std::string label = d.contains("label") ? std::string(d.at("label").as_string()) : "";
        const std::string uid = d.contains("uid") ? std::string(d.at("uid").as_string()) : "";

        // Human-readable account level mapping per OKX docs:
        // 1 = Simple (spot only), 2 = Single-currency margin,
        // 3 = Multi-currency margin, 4 = Portfolio margin.
        const char* lvl_name = "Unknown";
        bool derivatives_allowed = false;
        if (acct_lv == "1") {
            lvl_name = "Simple (spot only)";
        } else if (acct_lv == "2") {
            lvl_name = "Single-currency margin";
            derivatives_allowed = true;
        } else if (acct_lv == "3") {
            lvl_name = "Multi-currency margin";
            derivatives_allowed = true;
        } else if (acct_lv == "4") {
            lvl_name = "Portfolio margin";
            derivatives_allowed = true;
        }

        ygg::log::info("[OKX account/config] uid={} label='{}' acctLv={} ({}) perm={} posMode={}",
                       uid, label, acct_lv, lvl_name, perm, pos_mode);

        if (!derivatives_allowed) {
            ygg::log::warn("[OKX account/config] ACCOUNT CANNOT TRADE DERIVATIVES — "
                           "level {} is spot-only. Perp/futures/options orders will reject "
                           "with sCode=51155 'local compliance requirements' (the real cause "
                           "is the account level, not geo-compliance). Upgrade via OKX UI: "
                           "Demo Trading → Account Mode → Single-currency margin or higher.",
                           acct_lv);
        }
    } catch (const std::exception& e) {
        ygg::log::warn("[OKX account/config] fetch failed: {}", e.what());
    }
}

}  // namespace heimdall::adapter

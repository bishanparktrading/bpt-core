#include "order_gateway/adapter/deribit/deribit_order_adapter.h"

#include "order_gateway/adapter/common/credentials.h"
#include "order_gateway/adapter/deribit/deribit_action_encoder.h"

#include <messages/ExchangeId.h>
#include <messages/ExecStatus.h>
#include <messages/OrderSide.h>
#include <messages/OrderType.h>
#include <messages/RejectReason.h>

#include <boost/json.hpp>
#include <bpt_common/util/strings.h>
#include <bpt_common/util/tsc_clock.h>
#include <chrono>
#include <string>

namespace bpt::order_gateway::adapter {

namespace json = boost::json;

using bpt::common::util::hex8;

using bpt::common::util::WallClock;

DeribitOrderAdapter::DeribitOrderAdapter(const config::AdapterConfig& cfg, const ExchangeCredentials& creds)
    : OrderAdapterBase(cfg),
      client_id_(creds.client_id),
      client_secret_(creds.client_secret),
      ws_client_(ioc_, ssl_ctx_, cfg_) {
    session_prefix_ = hex8(static_cast<uint32_t>(WallClock::now_s()));

    decoder_.on_exec_event = [this](const ExecEvent& ev) {
        if (!exec_queue_.try_push(ev))
            bpt::common::log::error("[Deribit] exec_queue full — dropped ExecEvent order_id={}", ev.order_id);
    };

    ws_client_.set_login_msg_builder([this] {
        return deribit::build_auth_msg(client_id_, client_secret_, jsonrpc_id_.fetch_add(1, std::memory_order_relaxed));
    });
    ws_client_.set_message_handler(
        [this](const std::string& payload, uint64_t recv_ns) { handle_message(payload, recv_ns); });
}

void DeribitOrderAdapter::handle_message(const std::string& payload, uint64_t recv_ns) {
    // NOTE: do NOT log the raw payload here. Deribit login/auth frames
    // carry client_secret in plaintext JSON; authenticated
    // subscription responses may embed refresh tokens. The downstream
    // parsing below emits structured logs for event type / JSON-RPC id
    // / error code / order state — that's the right diagnostic surface.
    // Raw-payload visibility belongs behind a build flag or a dev-only
    // debug channel, not an INFO log.
    auto root = json::parse(payload);
    if (!root.is_object())
        return;
    const auto& obj = root.as_object();

    // Notification methods: heartbeat + subscription channel pushes
    if (auto method_it = obj.find("method"); method_it != obj.end()) {
        const std::string method = std::string(method_it->value().as_string());

        if (method == "heartbeat") {
            auto params_it = obj.find("params");
            if (params_it != obj.end() && params_it->value().is_object()) {
                auto type_it = params_it->value().as_object().find("type");
                if (type_it != params_it->value().as_object().end() &&
                    std::string(type_it->value().as_string()) == "test_request") {
                    ws_client_.send(deribit::build_test_response(jsonrpc_id_.fetch_add(1, std::memory_order_relaxed)));
                }
            }
            return;
        }

        if (method == "subscription") {
            auto params_it = obj.find("params");
            if (params_it == obj.end() || !params_it->value().is_object())
                return;
            const auto& params = params_it->value().as_object();
            auto channel_it = params.find("channel");
            if (channel_it == params.end())
                return;
            if (std::string(channel_it->value().as_string()) == "user.orders.any.raw") {
                auto data_it = params.find("data");
                if (data_it != params.end() && data_it->value().is_object())
                    decoder_.handle_subscription_event(data_it->value().as_object(), recv_ns);
            }
        }
        return;
    }

    // JSON-RPC responses (id-based)
    auto id_it = obj.find("id");
    if (id_it == obj.end())
        return;

    // Resolve any sync-waiter for this id (account snapshot path). We do
    // this BEFORE the order-response dispatch so a snapshot RPC can't
    // accidentally be re-routed to the exec decoder.
    const uint64_t rpc_id = id_it->value().to_number<uint64_t>();
    std::shared_ptr<std::promise<boost::json::value>> waiter;
    {
        std::lock_guard<std::mutex> lk(pending_responses_mu_);
        auto pit = pending_responses_.find(rpc_id);
        if (pit != pending_responses_.end()) {
            waiter = pit->second;
            pending_responses_.erase(pit);
        }
    }
    if (waiter) {
        // Hand the whole envelope back — caller picks `result` or `error`.
        waiter->set_value(root);
        return;
    }

    if (auto err_it = obj.find("error"); err_it != obj.end()) {
        const auto& err = err_it->value().as_object();
        int64_t code = 0;
        std::string errmsg;
        std::string data_str;
        if (auto cit = err.find("code"); cit != err.end())
            code = cit->value().to_number<int64_t>();
        if (auto mit = err.find("message"); mit != err.end())
            errmsg = std::string(mit->value().as_string());
        // Deribit's data field on -32602 carries `reason` + `param` — those
        // are the only useful breadcrumb when the params are malformed.
        if (auto dit = err.find("data"); dit != err.end())
            data_str = json::serialize(dit->value());
        bpt::common::log::error("DeribitOrderAdapter: JSON-RPC error id={} code={} msg={} data={}",
                                 rpc_id,
                                 code,
                                 errmsg,
                                 data_str);
        return;
    }

    auto result_it = obj.find("result");
    if (result_it == obj.end() || !result_it->value().is_object())
        return;
    const auto& res = result_it->value().as_object();

    // Auth response
    if (res.find("access_token") != res.end()) {
        bpt::common::log::info("DeribitOrderAdapter: authenticated successfully");
        logged_in_.store(true, std::memory_order_release);

        const auto next_id = [this] {
            return jsonrpc_id_.fetch_add(1, std::memory_order_relaxed);
        };
        ws_client_.send(deribit::build_simple_rpc("private/enable_cancel_on_disconnect", "", next_id()));
        ws_client_.send(deribit::build_simple_rpc("public/set_heartbeat", "{\"interval\":10}", next_id()));
        ws_client_.send(
            deribit::build_simple_rpc("private/subscribe", "{\"channels\":[\"user.orders.any.raw\"]}", next_id()));

        // Drain pending sends one-at-a-time, removing each AFTER a
        // successful dispatch. If a mid-drain failure throws, we leave
        // the remaining unsent frames queued for the next login attempt
        // AND drop the frame that threw (conservative — the bytes may
        // have reached the wire before the throw, and replaying would
        // risk a duplicate order even with Deribit's label dedup).
        //
        // Previous implementation used a single for-loop over the whole
        // vector + clear() after the loop; if send() threw mid-loop,
        // the clear didn't run and on reconnect EVERY frame in the
        // queue (including ones already successfully sent) got replayed.
        // That was the idempotency hole.
        std::lock_guard<std::mutex> lk(pending_mu_);
        while (!pending_sends_.empty()) {
            try {
                if (!ws_client_.send(pending_sends_.front()))
                    break;
            } catch (const std::exception& e) {
                bpt::common::log::error(
                    "DeribitOrderAdapter: mid-drain send threw ({}) — "
                    "dropping frame, {} remaining queued",
                    e.what(),
                    pending_sends_.size() - 1);
                pending_sends_.erase(pending_sends_.begin());
                break;
            }
            pending_sends_.erase(pending_sends_.begin());
        }
        return;
    }

    // Order response (private/buy or private/sell)
    if (auto order_it = res.find("order"); order_it != res.end())
        decoder_.handle_order_response(order_it->value().as_object(), recv_ns);
}

void DeribitOrderAdapter::connect_and_run() {
    logged_in_.store(false, std::memory_order_relaxed);
    decoder_.reset();
    ws_client_.run(stop_flag_, connected_);
}

void DeribitOrderAdapter::send_new_order(const bpt::messages::NewOrder& order) {
    const std::string exchange_symbol = order.getExchangeSymbolAsString();
    const std::string label = session_prefix_ + "G" + std::to_string(order.orderId());
    decoder_.register_order(label, order.orderId());

    const deribit::OrderSpec spec{
        exchange_symbol,
        order.side(),
        order.orderType(),
        order.timeInForce(),
        order.price(),
        order.quantity(),
        label,
    };
    const std::string frame = deribit::build_new_order_msg(spec, jsonrpc_id_.fetch_add(1, std::memory_order_relaxed));

    auto emit_rejection = [&]() {
        const uint64_t ts = bpt::common::util::WallClock::now_ns();
        ExecEvent rej{};
        rej.order_id = order.orderId();
        rej.exchange_id = bpt::messages::ExchangeId::DERIBIT;
        rej.instrument_id = 0;
        rej.local_ts_ns = ts;
        rej.exchange_ts_ns = ts;
        rej.side = order.side();
        rej.order_type = order.orderType();
        rej.status = bpt::messages::ExecStatus::REJECTED;
        rej.reject_reason = bpt::messages::RejectReason::EXCHANGE_ERROR;
        if (!exec_queue_.try_push(rej))
            bpt::common::log::error("[Deribit] exec_queue full — dropped rejection order_id={}", rej.order_id);
    };

    if (!logged_in_.load(std::memory_order_acquire)) {
        bpt::common::log::info(
            "DeribitOrderAdapter: queuing order {} (not yet "
            "authenticated)",
            order.orderId());
        std::lock_guard<std::mutex> lk(pending_mu_);
        pending_sends_.push_back(frame);
        return;
    }

    try {
        if (!ws_client_.send(frame)) {
            bpt::common::log::warn(
                "DeribitOrderAdapter: send_new_order: WS not "
                "connected, rejecting order={}",
                order.orderId());
            emit_rejection();
        }
    } catch (const std::exception& e) {
        bpt::common::log::error("DeribitOrderAdapter: send_new_order failed: {}", e.what());
        emit_rejection();
    }
}

void DeribitOrderAdapter::send_cancel(const bpt::messages::CancelOrder& cancel, const std::string& /*native_symbol*/) {
    // Deribit cancel uses exchange order_id, not instrument symbol.
    const std::string exch_oid = decoder_.get_exchange_order_id(cancel.orderId());
    if (exch_oid.empty()) {
        bpt::common::log::warn(
            "DeribitOrderAdapter: send_cancel: no exchange "
            "order_id for order={}",
            cancel.orderId());
        return;
    }

    const std::string frame = deribit::build_cancel_msg(exch_oid, jsonrpc_id_.fetch_add(1, std::memory_order_relaxed));
    try {
        ws_client_.send(frame);
    } catch (const std::exception& e) {
        bpt::common::log::error("DeribitOrderAdapter: send_cancel failed: {}", e.what());
    }
}

void DeribitOrderAdapter::send_cancel_all(uint64_t instrument_id) {
    bpt::common::log::warn(
        "DeribitOrderAdapter: send_cancel_all called "
        "instrument_id={} — not supported without instrument name",
        instrument_id);
}

void DeribitOrderAdapter::send_modify(const bpt::messages::ModifyOrder& modify, const std::string& /*native_symbol*/) {
    const std::string exch_oid = decoder_.get_exchange_order_id(modify.orderId());
    if (exch_oid.empty()) {
        bpt::common::log::warn(
            "DeribitOrderAdapter: send_modify: no exchange "
            "order_id for order={}",
            modify.orderId());
        return;
    }

    const std::string frame = deribit::build_edit_msg(exch_oid,
                                                      modify.newPrice(),
                                                      modify.newQuantity(),
                                                      jsonrpc_id_.fetch_add(1, std::memory_order_relaxed));
    try {
        ws_client_.send(frame);
    } catch (const std::exception& e) {
        bpt::common::log::error("DeribitOrderAdapter: send_modify failed: {}", e.what());
    }
}

json::value DeribitOrderAdapter::send_and_wait(const std::string& method,
                                               const std::string& params_json,
                                               std::chrono::milliseconds timeout) {
    if (!logged_in_.load(std::memory_order_acquire))
        throw std::runtime_error("send_and_wait called before auth completed");

    const uint64_t id = jsonrpc_id_.fetch_add(1, std::memory_order_relaxed);
    auto promise = std::make_shared<std::promise<json::value>>();
    auto future = promise->get_future();

    {
        std::lock_guard<std::mutex> lk(pending_responses_mu_);
        pending_responses_.emplace(id, promise);
    }

    const std::string frame = deribit::build_simple_rpc(method, params_json, id);
    if (!ws_client_.send(frame)) {
        std::lock_guard<std::mutex> lk(pending_responses_mu_);
        pending_responses_.erase(id);
        throw std::runtime_error("ws_client send failed for method=" + method);
    }

    if (future.wait_for(timeout) != std::future_status::ready) {
        std::lock_guard<std::mutex> lk(pending_responses_mu_);
        pending_responses_.erase(id);
        throw std::runtime_error("timeout waiting for response to method=" + method);
    }
    return future.get();
}

namespace {

int64_t to_e8(double v) {
    return static_cast<int64_t>(std::llround(v * 1e8));
}

// Helper: extract a finite double from a json field (handles double / int / null gracefully).
double j_double(const json::object& o, std::string_view key) {
    auto it = o.find(key);
    if (it == o.end() || it->value().is_null())
        return 0.0;
    if (it->value().is_double())
        return it->value().as_double();
    if (it->value().is_int64())
        return static_cast<double>(it->value().as_int64());
    if (it->value().is_uint64())
        return static_cast<double>(it->value().as_uint64());
    return 0.0;
}

std::string j_string(const json::object& o, std::string_view key) {
    auto it = o.find(key);
    if (it == o.end() || !it->value().is_string())
        return {};
    return std::string(it->value().as_string());
}

}  // namespace

AccountSnapshotData DeribitOrderAdapter::fetch_account_snapshot(uint64_t correlation_id) {
    AccountSnapshotData snap;
    snap.exchange_id = bpt::messages::ExchangeId::DERIBIT;
    snap.correlation_id = correlation_id;
    snap.timestamp_ns = bpt::common::util::WallClock::now_ns();

    if (!logged_in_.load(std::memory_order_acquire)) {
        bpt::common::log::warn(
            "DeribitOrderAdapter: fetch_account_snapshot called before auth — returning empty");
        return snap;
    }

    using namespace std::chrono_literals;
    constexpr auto kTimeout = 5000ms;

    // Per-currency summaries (balance + equity). Deribit's get_account_summaries
    // returns one entry per currency (BTC, ETH, USDC, etc.). Aggregate USD
    // equity across them goes into total_equity_e8 / available_balance_e8.
    try {
        const auto env = send_and_wait("private/get_account_summaries", "{}", kTimeout);
        const auto& obj = env.as_object();
        if (auto err_it = obj.find("error"); err_it != obj.end()) {
            bpt::common::log::error(
                "DeribitOrderAdapter: get_account_summaries error: {}",
                json::serialize(err_it->value()));
        } else if (auto res_it = obj.find("result"); res_it != obj.end() && res_it->value().is_object()) {
            const auto& res = res_it->value().as_object();
            auto sum_it = res.find("summaries");
            if (sum_it != res.end() && sum_it->value().is_array()) {
                for (const auto& v : sum_it->value().as_array()) {
                    if (!v.is_object())
                        continue;
                    const auto& s = v.as_object();
                    CurrencyBalance cb;
                    cb.ccy = j_string(s, "currency");
                    cb.equity_e8 = to_e8(j_double(s, "equity"));
                    cb.available_balance_e8 = to_e8(j_double(s, "available_funds"));
                    snap.currency_balances.push_back(cb);
                }
            }
        }
    } catch (const std::exception& e) {
        bpt::common::log::error("DeribitOrderAdapter: get_account_summaries failed: {}", e.what());
    }

    // Open positions per currency. Deribit's get_positions requires a currency
    // filter — query for each currency we observed in the summary. Empty
    // currencies are skipped so we don't burn a useless RPC.
    for (const auto& cb : snap.currency_balances) {
        if (cb.ccy.empty())
            continue;
        try {
            const std::string params = "{\"currency\":\"" + cb.ccy + "\"}";
            const auto env = send_and_wait("private/get_positions", params, kTimeout);
            const auto& obj = env.as_object();
            if (auto err_it = obj.find("error"); err_it != obj.end()) {
                bpt::common::log::error("DeribitOrderAdapter: get_positions({}) error: {}",
                                         cb.ccy,
                                         json::serialize(err_it->value()));
                continue;
            }
            auto res_it = obj.find("result");
            if (res_it == obj.end() || !res_it->value().is_array())
                continue;
            for (const auto& v : res_it->value().as_array()) {
                if (!v.is_object())
                    continue;
                const auto& p = v.as_object();
                const double size = j_double(p, "size");
                if (size == 0.0)
                    continue;
                AccountPosition ap;
                ap.exchange_symbol = j_string(p, "instrument_name");
                ap.net_qty_e8 = to_e8(size);
                ap.avg_entry_price_e8 = to_e8(j_double(p, "average_price"));
                ap.unrealized_pnl_e8 = to_e8(j_double(p, "floating_profit_loss"));
                snap.positions.push_back(ap);
            }
        } catch (const std::exception& e) {
            bpt::common::log::error("DeribitOrderAdapter: get_positions({}) failed: {}", cb.ccy, e.what());
        }
    }

    // Aggregate USD equity / available across currencies. Deribit's per-ccy
    // equity is already in coin-units (BTC, ETH, etc.); converting to USDT
    // would require a spot price lookup. For now the console's `equity`
    // field gets the largest single-currency equity, while currency_balances
    // carries the per-ccy detail. The strategy reads positions directly.
    // Revisit when USD-aggregation across crypto-collateral accounts matters.
    int64_t largest_eq = 0;
    int64_t largest_avail = 0;
    for (const auto& cb : snap.currency_balances) {
        if (cb.equity_e8 > largest_eq) {
            largest_eq = cb.equity_e8;
            largest_avail = cb.available_balance_e8;
        }
    }
    snap.total_equity_e8 = largest_eq;
    snap.available_balance_e8 = largest_avail;

    bpt::common::log::info(
        "DeribitOrderAdapter: fetched account snapshot — currencies={} positions={}",
        snap.currency_balances.size(),
        snap.positions.size());
    return snap;
}

}  // namespace bpt::order_gateway::adapter

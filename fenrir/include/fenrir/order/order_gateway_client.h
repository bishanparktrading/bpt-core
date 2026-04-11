#pragma once

#include <Aeron.h>
#include <FragmentAssembler.h>

#include <bifrost_protocol/AccountSnapshot.h>
#include <bifrost_protocol/AccountSnapshotRequest.h>
#include <bifrost_protocol/CancelAll.h>
#include <bifrost_protocol/CancelOrder.h>
#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/ExecutionReport.h>
#include <bifrost_protocol/ModifyOrder.h>
#include <bifrost_protocol/NewOrder.h>
#include <bifrost_protocol/OrderGatewayHeartbeat.h>
#include <bifrost_protocol/OrderSide.h>
#include <bifrost_protocol/OrderType.h>
#include <bifrost_protocol/TimeInForce.h>

#include <functional>
#include <memory>
#include <string>

namespace fenrir::order {

class OrderGatewayClient {
public:
    using OnExecReportFn = std::function<void(const bifrost::protocol::ExecutionReport&)>;
    using OnHeartbeatFn = std::function<void(const bifrost::protocol::OrderGatewayHeartbeat&)>;
    using OnAccountSnapshotFn = std::function<void(bifrost::protocol::AccountSnapshot&)>;

    OrderGatewayClient(std::shared_ptr<aeron::Aeron> aeron,
                       const std::string& channel,
                       int order_stream,
                       int exec_report_stream,
                       int heartbeat_stream,
                       int account_snapshot_stream,
                       int pub_timeout_ms = 5000,
                       int pub_poll_interval_ms = 10);

    // Send a new order to the order gateway.
    // exchange_symbol must be the exchange-native symbol (e.g. "BTCUSDT" for Binance,
    // "BTC-USDT" for OKX, "BTC" for Hyperliquid), resolved by Fenrir from its Sindri
    // refdata cache before calling this method.
    //
    // Returns false (and logs a warning) if the order fails pre-flight validation:
    //   - quantity == 0
    //   - price <= 0 for LIMIT or POST_ONLY orders
    //   - exchange_symbol is empty
    // Callers must undo any order-state bookkeeping if this returns false.
    [[nodiscard]] bool send_new_order(uint64_t order_id,
                                      bifrost::protocol::ExchangeId::Value exchange_id,
                                      uint64_t instrument_id,
                                      bifrost::protocol::OrderSide::Value side,
                                      bifrost::protocol::OrderType::Value order_type,
                                      bifrost::protocol::TimeInForce::Value tif,
                                      int64_t price,
                                      uint64_t quantity,
                                      const std::string& exchange_symbol);

    // Cancel a specific order.
    void send_cancel(uint64_t order_id, bifrost::protocol::ExchangeId::Value exchange_id, uint64_t instrument_id);

    // Cancel all orders (exchange_id = ALL cancels every venue, instrument_id=0 = all instruments).
    void send_cancel_all(bifrost::protocol::ExchangeId::Value exchange_id, uint64_t instrument_id);

    // Modify an existing order's price and/or quantity in place (OKX amend-order).
    // Preserves queue priority when only price changes within exchange tolerance.
    void send_modify(uint64_t order_id,
                     bifrost::protocol::ExchangeId::Value exchange_id,
                     uint64_t instrument_id,
                     int64_t new_price,
                     uint64_t new_quantity);

    // Request an account snapshot from Heimdall for the given exchange.
    // Heimdall will fetch from the exchange REST API and publish an AccountSnapshot
    // on the account_snapshot stream.
    void send_account_snapshot_request(bifrost::protocol::ExchangeId::Value exchange_id, uint64_t correlation_id);

    // Poll for execution reports, heartbeats, and account snapshots.
    int poll(int fragment_limit = 10);

    // Callbacks — set before calling poll().
    OnExecReportFn on_exec_report;
    OnHeartbeatFn on_heartbeat;
    OnAccountSnapshotFn on_account_snapshot;

    [[nodiscard]] uint64_t last_heartbeat_ns() const { return last_heartbeat_ns_; }

private:
    void handle_exec_report_fragment(aeron::AtomicBuffer& buf,
                                     aeron::util::index_t offset,
                                     aeron::util::index_t length,
                                     aeron::Header& hdr);
    void handle_heartbeat_fragment(aeron::AtomicBuffer& buf,
                                   aeron::util::index_t offset,
                                   aeron::util::index_t length,
                                   aeron::Header& hdr);
    void handle_account_snapshot_fragment(aeron::AtomicBuffer& buf,
                                          aeron::util::index_t offset,
                                          aeron::util::index_t length,
                                          aeron::Header& hdr);

    std::shared_ptr<aeron::Publication> order_pub_;
    std::shared_ptr<aeron::Subscription> exec_report_sub_;
    std::shared_ptr<aeron::Subscription> heartbeat_sub_;
    std::shared_ptr<aeron::Subscription> account_snapshot_sub_;
    std::unique_ptr<aeron::FragmentAssembler> exec_assembler_;
    std::unique_ptr<aeron::FragmentAssembler> hb_assembler_;
    std::unique_ptr<aeron::FragmentAssembler> account_snapshot_assembler_;
    uint64_t last_heartbeat_ns_{0};
};

}  // namespace fenrir::order

#pragma once

#include "heimdall/adapter/common/i_order_adapter.h"
#include "heimdall/messaging/exec_report_publisher.h"
#include "heimdall/metrics/metrics.h"
#include "heimdall/order/order_state_manager.h"
#include "heimdall/risk/risk_checker.h"

#include <bifrost_protocol/CancelAll.h>
#include <bifrost_protocol/CancelOrder.h>
#include <bifrost_protocol/ModifyOrder.h>
#include <bifrost_protocol/NewOrder.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace heimdall::order {

// OrderProcessor is the single point of coordination for all order lifecycle
// events on the hot-path thread.  It owns the logic connecting the four core
// components — ExecReportPublisher, OrderStateManager, RiskChecker, and
// HeimdallMetrics — so that main.cpp is purely wiring and polling.
//
// Threading: all public methods must be called from the same thread.  The
// underlying components (OrderStateManager, RiskChecker) are not thread-safe
// by design; the Aeron poll loop provides the single-writer guarantee.
//
// Lifetime: OrderProcessor holds non-owning references to all dependencies.
// The caller (main) is responsible for ensuring they outlive this object.
class OrderProcessor {
public:
    // All dependencies are non-owning references.  The adapters vector is also
    // referenced rather than copied so that the processor always sees the live
    // set without needing to be rebuilt if an adapter is added at runtime.
    OrderProcessor(messaging::ExecReportPublisher& exec_pub,
                   OrderStateManager& state_mgr,
                   risk::RiskChecker& risk_checker,
                   metrics::HeimdallMetrics& metrics,
                   const std::vector<std::shared_ptr<adapter::IOrderAdapter>>& adapters);

    // Called by each adapter's on_exec_event callback when an exchange event
    // arrives (ack, partial fill, fill, cancel, reject).
    //
    // Flow:
    //   1. Map ExecStatus → OrderLifecycle.
    //   2. Update state manager with new lifecycle + fill quantities.
    //   3. Release the open-order risk slot for terminal states.
    //   4. Forward the raw exec report to Fenrir via ExecReportPublisher.
    //   5. Increment exec_report metrics counter.
    //   6. On ACKED: record order ACK round-trip time (local_ts − created_ns).
    //   7. On terminal: remove order from state manager.
    void on_exec_event(const adapter::ExecEvent& ev);

    // Called when Fenrir sends a NewOrder on stream 3001.
    //
    // Flow:
    //   1. Risk check (size, notional, open-order count, rate limit).
    //      → Reject immediately with REJECTED exec report if check fails.
    //   2. Adapter lookup by exchange ID.
    //      → Reject with EXCHANGE_ERROR if adapter is absent or disconnected.
    //         The risk slot incremented in step 1 is released here to keep
    //         counters consistent.
    //   3. Insert order into state manager (PENDING lifecycle).
    //   4. Dispatch to adapter's send_new_order.
    void on_new_order(const bifrost::protocol::NewOrder& order);

    // Called when Fenrir sends a CancelOrder on stream 3001.
    // Looks up the exchange-native symbol from state (populated at NewOrder
    // time) so the adapter does not need to maintain its own symbol mapping.
    void on_cancel(const bifrost::protocol::CancelOrder& cancel);

    // Called when Fenrir sends a CancelAll on stream 3001.
    //
    // Two modes:
    //   - ExchangeId::ALL  — kill switch: disables trading via RiskChecker,
    //     then iterates all open orders and cancels each one individually.
    //   - Specific exchange — cancels all orders on that venue for the given
    //     instrument_id (0 = all instruments).
    void on_cancel_all(const bifrost::protocol::CancelAll& msg);

    // Called when Fenrir sends a ModifyOrder on stream 3001.
    // Looks up the exchange-native symbol from state, same as on_cancel.
    void on_modify(const bifrost::protocol::ModifyOrder& modify);

    // Scans for orders stuck in ACKED state for longer than stale_timeout_ns.
    // For each stale order:
    //   - Logs a warning with the order ID, exchange, and age.
    //   - Releases the open-order risk slot.
    //   - Publishes a synthetic CANCELLED exec report to Fenrir so it can
    //     reconcile its own position and order book.
    //   - Removes the order from state.
    //
    // Called every poll iteration from main; the overhead is proportional to
    // the number of open orders, which is bounded by RiskChecker limits.
    void check_stale_orders(uint64_t stale_timeout_ns);

private:
    // Linear scan over adapters_ — the list is short (≤4 exchanges) so this
    // is faster than a hash map in practice.
    [[nodiscard]] adapter::IOrderAdapter* find_adapter(bifrost::protocol::ExchangeId::Value id) const;

    // Maps the wire-format ExecStatus onto the internal OrderLifecycle enum.
    // Centralised here so adapters can use the raw protocol type without
    // knowing about OrderLifecycle.
    static OrderLifecycle exec_status_to_lifecycle(bifrost::protocol::ExecStatus::Value status);

    // String labels used for Prometheus metric tags — must be stable literals.
    static const char* lifecycle_str(OrderLifecycle lc);
    static const char* exchange_str(bifrost::protocol::ExchangeId::Value id);

    messaging::ExecReportPublisher& exec_pub_;
    OrderStateManager& state_mgr_;
    risk::RiskChecker& risk_checker_;
    metrics::HeimdallMetrics& metrics_;
    // O(1) adapter lookup by ExchangeId value (0=ALL unused, 1=BINANCE, …, 4=DERIBIT).
    std::array<adapter::IOrderAdapter*, 5> adapter_by_id_{};
    // Pre-allocated scratch buffer for check_stale_orders to avoid hot-path heap alloc.
    std::vector<uint64_t> stale_ids_scratch_;
};

}  // namespace heimdall::order

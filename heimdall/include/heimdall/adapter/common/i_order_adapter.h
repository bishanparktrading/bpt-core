#pragma once

#include "heimdall/adapter/common/account_snapshot_data.h"
#include "heimdall/order/order_state_manager.h"

#include <bifrost_protocol/CancelOrder.h>
#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/ExecStatus.h>
#include <bifrost_protocol/FeeCurrency.h>
#include <bifrost_protocol/ModifyOrder.h>
#include <bifrost_protocol/NewOrder.h>
#include <bifrost_protocol/OrderSide.h>
#include <bifrost_protocol/OrderType.h>
#include <bifrost_protocol/RejectReason.h>

#include <cstdint>
#include <functional>
#include <string>

namespace heimdall::adapter {

// Exec event fired by adapters on every exchange report (ack, fill, cancel, reject).
// All fields use the same fixed-point scale as the SBE messages (1e8 for price/qty).
struct ExecEvent {
    uint64_t order_id{0};
    uint64_t exchange_order_id{0};
    bifrost::protocol::ExchangeId::Value exchange_id;
    uint64_t instrument_id{0};
    bifrost::protocol::ExecStatus::Value status;
    bifrost::protocol::OrderSide::Value side;
    bifrost::protocol::OrderType::Value order_type;
    int64_t price{0};
    uint64_t filled_qty{0};
    uint64_t remaining_qty{0};
    bifrost::protocol::RejectReason::Value reject_reason;
    int64_t fee{0};
    bifrost::protocol::FeeCurrency::Value fee_currency;
    uint64_t exchange_ts_ns{0};
    uint64_t local_ts_ns{0};
};

class IOrderAdapter {
public:
    virtual ~IOrderAdapter() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    // Place, cancel, modify orders.  Thread-safe — called from the hot-path thread.
    virtual void send_new_order(const bifrost::protocol::NewOrder& order) = 0;
    virtual void send_cancel(const bifrost::protocol::CancelOrder& cancel, const std::string& native_symbol) = 0;
    virtual void send_cancel_all(uint64_t instrument_id) = 0;
    virtual void send_modify(const bifrost::protocol::ModifyOrder& modify, const std::string& native_symbol) = 0;

    [[nodiscard]] virtual bifrost::protocol::ExchangeId::Value exchange_id() const = 0;
    [[nodiscard]] virtual const char* exchange_name() const = 0;
    [[nodiscard]] virtual bool is_connected() const = 0;

    // Drain all pending exec events from the adapter's IO thread into the
    // caller's thread.  Call this from the main poll loop on every iteration.
    // fn is invoked once per event; returns the number of events drained.
    virtual int drain_exec_events(const std::function<void(const ExecEvent&)>& fn) = 0;

    // Fetch current account positions and balance from the exchange REST API.
    // Blocking — must be called from a dedicated thread, not the poll loop.
    // Returns a populated AccountSnapshotData; throws std::exception on failure.
    virtual AccountSnapshotData fetch_account_snapshot(uint64_t correlation_id) = 0;
};

}  // namespace heimdall::adapter

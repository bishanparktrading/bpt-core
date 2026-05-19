#pragma once

/// \file
/// \brief Send-work items handed from the main poll thread to the
///        per-adapter send-executor thread.
///
/// Captures everything needed by the venue-specific `do_send_*_blocking`
/// hooks. POD-ish: integer/enum scalars + a `std::string` for the
/// exchange-native symbol. Heap allocation on the symbol is acceptable at
/// ogw rates (~10s/sec) and keeps the queue value-typed.

#include <messages/OrderSide.h>
#include <messages/OrderType.h>
#include <messages/TimeInForce.h>

#include <cstdint>
#include <string>
#include <variant>

namespace bpt::order_gateway::util {

struct NewOrderRequest {
    uint64_t order_id{0};
    uint64_t instrument_id{0};
    int64_t price{0};
    uint64_t quantity{0};
    bpt::messages::OrderSide::Value side;
    bpt::messages::OrderType::Value order_type;
    bpt::messages::TimeInForce::Value tif;
    uint8_t exec_inst{0};
    std::string exchange_symbol;
};

struct CancelRequest {
    uint64_t order_id{0};
    std::string native_symbol;
};

struct CancelAllRequest {
    uint64_t instrument_id{0};
};

struct ModifyRequest {
    uint64_t order_id{0};
    int64_t new_price{0};
    uint64_t new_quantity{0};
    std::string native_symbol;
};

using SendWorkItem = std::variant<NewOrderRequest, CancelRequest, CancelAllRequest, ModifyRequest>;

}  // namespace bpt::order_gateway::util

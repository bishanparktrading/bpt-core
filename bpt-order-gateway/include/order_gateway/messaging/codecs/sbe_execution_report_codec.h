#pragma once

#include "bpt_common/codec/codec.h"

#include <messages/ExecStatus.h>
#include <messages/ExchangeId.h>
#include <messages/OrderSide.h>
#include <messages/OrderType.h>
#include <messages/RejectReason.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace bpt::order_gateway::messaging {

/// Domain shape for ExecutionReport — flat struct mirroring the SBE
/// fields. ExecReportPublisher composes the 15-arg port method into
/// this struct before handing to the codec.
struct ExecutionReportMsg {
    uint64_t order_id;
    uint64_t exchange_order_id;
    bpt::messages::ExchangeId::Value exchange_id;
    uint64_t instrument_id;
    bpt::messages::ExecStatus::Value status;
    bpt::messages::OrderSide::Value side;
    bpt::messages::OrderType::Value order_type;
    int64_t  price;
    uint64_t filled_qty;
    uint64_t remaining_qty;
    bpt::messages::RejectReason::Value reject_reason;
    int64_t  fee;
    std::string fee_currency;  ///< ≤ 8 chars; padded/truncated to fit Char8 slot
    uint64_t exchange_ts_ns;
    uint64_t local_ts_ns;
};

class SbeExecutionReportCodec {
public:
    std::span<const std::byte> encode(const ExecutionReportMsg&, std::span<std::byte> scratch);
    ExecutionReportMsg          decode(std::span<const std::byte>);

    static constexpr std::size_t kRecommendedScratchSize = 256;
};

static_assert(bpt::common::codec::Codec<SbeExecutionReportCodec, ExecutionReportMsg>);

}  // namespace bpt::order_gateway::messaging

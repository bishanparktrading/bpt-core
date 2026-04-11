#pragma once

#include <Aeron.h>

#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/ExecStatus.h>
#include <bifrost_protocol/FeeCurrency.h>
#include <bifrost_protocol/OrderSide.h>
#include <bifrost_protocol/OrderType.h>
#include <bifrost_protocol/RejectReason.h>

#include <memory>
#include <string>

namespace heimdall::messaging {

class ExecReportPublisher {
public:
    ExecReportPublisher(std::shared_ptr<aeron::Aeron> aeron, const std::string& channel, int stream_id);

    void publish(uint64_t order_id,
                 uint64_t exchange_order_id,
                 bifrost::protocol::ExchangeId::Value exchange_id,
                 uint64_t instrument_id,
                 bifrost::protocol::ExecStatus::Value status,
                 bifrost::protocol::OrderSide::Value side,
                 bifrost::protocol::OrderType::Value order_type,
                 int64_t price,
                 uint64_t filled_qty,
                 uint64_t remaining_qty,
                 bifrost::protocol::RejectReason::Value reject_reason,
                 int64_t fee,
                 bifrost::protocol::FeeCurrency::Value fee_currency,
                 uint64_t exchange_ts_ns,
                 uint64_t local_ts_ns);

private:
    std::shared_ptr<aeron::Publication> publication_;
};

}  // namespace heimdall::messaging

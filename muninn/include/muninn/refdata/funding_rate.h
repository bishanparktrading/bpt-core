#pragma once

#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/InstrumentType.h>

#include <cstdint>

namespace muninn::refdata {

struct FeeScheduleState {
    bifrost::protocol::ExchangeId::Value exchange_id;
    uint64_t instrument_id;  // 0 = applies to all instruments on this exchange
    bifrost::protocol::InstrumentType::Value instrument_type;
    int16_t maker_fee_bps;  // signed; negative = rebate
    int16_t taker_fee_bps;
    uint64_t updated_ts;  // UTC nanosecond epoch when fetched from exchange
};

}  // namespace muninn::refdata

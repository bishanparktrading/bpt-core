#pragma once

#include <Aeron.h>

#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/RefDataErrorType.h>

#include <cstdint>
#include <memory>

namespace muninn::messaging {

// Publishes RefDataReady (id=16) and RefDataError (id=17) on stream 1006.
class MuninnStatusPublisher {
public:
    MuninnStatusPublisher(std::shared_ptr<aeron::Aeron> aeron, const std::string& channel, int stream_id);

    // Published once after all enabled exchange adapters have completed
    // their initial snapshot fetch.
    void publish_ready(uint8_t exchanges_loaded,  // bitmask bit0=BINANCE, bit1=OKX, bit2=HYPERLIQUID
                       uint16_t instrument_count,
                       bool fee_schedules_loaded);

    // Published whenever a runtime error occurs that Fenrir must act on.
    void publish_error(bifrost::protocol::RefDataErrorType::Value error_type,
                       bifrost::protocol::ExchangeId::Value exchange_id,
                       uint64_t instrument_id = 0);

private:
    std::shared_ptr<aeron::Publication> publication_;
};

}  // namespace muninn::messaging

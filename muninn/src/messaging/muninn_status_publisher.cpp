#include "muninn/messaging/muninn_status_publisher.h"

#include "muninn/messaging/sbe_utils.h"

#include <bifrost_protocol/MessageHeader.h>
#include <bifrost_protocol/RefDataError.h>
#include <bifrost_protocol/RefDataReady.h>

#include <spdlog/spdlog.h>
#include <yggdrasil/aeron/aeron_utils.h>
#include <yggdrasil/util/tsc_clock.h>

namespace muninn::messaging {

MuninnStatusPublisher::MuninnStatusPublisher(std::shared_ptr<aeron::Aeron> aeron,
                                             const std::string& channel,
                                             int stream_id) {
    publication_ = ygg::aeron::wait_for_publication(aeron, channel, stream_id);
}

void MuninnStatusPublisher::publish_ready(uint8_t exchanges_loaded,
                                          uint16_t instrument_count,
                                          bool fee_schedules_loaded) {
    using namespace bifrost::protocol;

    constexpr std::size_t kBufSize = MessageHeader::encodedLength() + RefDataReady::sbeBlockLength();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    char buf[kBufSize];

    uint64_t now_ns = ygg::util::TscClock::now_epoch_ns();

    RefDataReady msg;
    msg.wrapAndApplyHeader(buf, 0, kBufSize)
        .timestampNs(now_ns)
        .exchangesLoaded(exchanges_loaded)
        .instrumentCount(instrument_count)
        .feeSchedulesLoaded(fee_schedules_loaded ? uint8_t{1} : uint8_t{0})
        .fundingRatesLoaded(uint8_t{0});  // Funding rates moved to Huginn; always 0 from Muninn

    aeron::AtomicBuffer ab(reinterpret_cast<uint8_t*>(buf), kBufSize);
    aeron_offer(*publication_, ab, static_cast<aeron::util::index_t>(kBufSize), "muninn_ready");

    spdlog::info("[Muninn] RefDataReady published exchanges_loaded=0x{:02x} instruments={} fee_schedules={}",
                 exchanges_loaded,
                 instrument_count,
                 fee_schedules_loaded);
}

void MuninnStatusPublisher::publish_error(bifrost::protocol::RefDataErrorType::Value error_type,
                                          bifrost::protocol::ExchangeId::Value exchange_id,
                                          uint64_t instrument_id) {
    using namespace bifrost::protocol;

    constexpr std::size_t kBufSize = MessageHeader::encodedLength() + RefDataError::sbeBlockLength();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    char buf[kBufSize];

    uint64_t now_ns = ygg::util::TscClock::now_epoch_ns();

    RefDataError msg;
    msg.wrapAndApplyHeader(buf, 0, kBufSize)
        .timestampNs(now_ns)
        .errorType(error_type)
        .exchangeId(exchange_id)
        .instrumentId(instrument_id);

    aeron::AtomicBuffer ab(reinterpret_cast<uint8_t*>(buf), kBufSize);
    aeron_offer(*publication_, ab, static_cast<aeron::util::index_t>(kBufSize), "muninn_error");

    spdlog::error("[Muninn] RefDataError published error_type={} exchange={} instrument_id={}",
                  RefDataErrorType::c_str(error_type),
                  ExchangeId::c_str(exchange_id),
                  instrument_id);
}

}  // namespace muninn::messaging

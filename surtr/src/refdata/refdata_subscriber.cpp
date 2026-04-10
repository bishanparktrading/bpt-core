#include "surtr/refdata/refdata_subscriber.h"

#include <bifrost_protocol/DeltaUpdateType.h>
#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/InstrumentType.h>
#include <bifrost_protocol/MessageHeader.h>
#include <bifrost_protocol/OptionSide.h>
#include <bifrost_protocol/RefDataDelta.h>
#include <bifrost_protocol/RefDataSnapshot.h>

#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>

namespace surtr::refdata {

static bifrost::protocol::ExchangeId::Value exchange_from_string(const std::string& ex) {
    using EX = bifrost::protocol::ExchangeId;
    if (ex == "BINANCE")
        return EX::BINANCE;
    if (ex == "OKX")
        return EX::OKX;
    if (ex == "HYPERLIQUID")
        return EX::HYPERLIQUID;
    if (ex == "DERIBIT")
        return EX::DERIBIT;
    return EX::NULL_VALUE;
}

static std::string trim_null(const char* data, size_t len) {
    std::string s(data, len);
    auto pos = s.find('\0');
    if (pos != std::string::npos)
        s.resize(pos);
    return s;
}

RefdataSubscriber::RefdataSubscriber(std::shared_ptr<aeron::Aeron> aeron,
                                     const std::string& snapshot_channel,
                                     int32_t snapshot_stream_id,
                                     const std::string& delta_channel,
                                     int32_t delta_stream_id) {
    const int64_t snap_id = aeron->addSubscription(snapshot_channel, snapshot_stream_id);
    const int64_t delta_id = aeron->addSubscription(delta_channel, delta_stream_id);

    for (int i = 0; i < 500; ++i) {
        if (!snapshot_sub_)
            snapshot_sub_ = aeron->findSubscription(snap_id);
        if (!delta_sub_)
            delta_sub_ = aeron->findSubscription(delta_id);
        if (snapshot_sub_ && delta_sub_)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (snapshot_sub_) {
        spdlog::info("[RefdataSubscriber] Snapshot subscription ready");
        snap_assembler_ = std::make_unique<aeron::FragmentAssembler>(
            [this](aeron::AtomicBuffer& buffer,
                   aeron::util::index_t offset,
                   aeron::util::index_t length,
                   aeron::Header& header) { on_snapshot_fragment(buffer, offset, length, header); });
    } else {
        spdlog::error("[RefdataSubscriber] Failed to create snapshot subscription");
    }

    if (delta_sub_)
        spdlog::info("[RefdataSubscriber] Delta subscription ready");
    else
        spdlog::error("[RefdataSubscriber] Failed to create delta subscription");
}

int RefdataSubscriber::poll(int fragment_limit) {
    int total = 0;
    if (snapshot_sub_ && snap_assembler_) {
        total += snapshot_sub_->poll(snap_assembler_->handler(), fragment_limit);
    }
    if (delta_sub_) {
        total +=
            delta_sub_->poll([this](const aeron::concurrent::AtomicBuffer& buffer,
                                    aeron::util::index_t offset,
                                    aeron::util::index_t length,
                                    const aeron::Header& header) { on_delta_fragment(buffer, offset, length, header); },
                             fragment_limit);
    }
    return total;
}

void RefdataSubscriber::on_snapshot_fragment(const aeron::concurrent::AtomicBuffer& buffer,
                                             aeron::util::index_t offset,
                                             aeron::util::index_t length,
                                             const aeron::Header& /*header*/) {
    using namespace bifrost::protocol;

    if (length < static_cast<aeron::util::index_t>(MessageHeader::encodedLength()))
        return;

    MessageHeader hdr;
    hdr.wrap(const_cast<char*>(reinterpret_cast<const char*>(buffer.buffer() + offset)),
             0,
             MessageHeader::sbeSchemaVersion(),
             static_cast<uint64_t>(length));

    if (hdr.templateId() != RefDataSnapshot::sbeTemplateId())
        return;

    RefDataSnapshot snap;
    snap.wrapForDecode(const_cast<char*>(reinterpret_cast<const char*>(buffer.buffer() + offset)),
                       MessageHeader::encodedLength(),
                       hdr.blockLength(),
                       hdr.version(),
                       static_cast<uint64_t>(length));

    auto& instruments = snap.instruments();
    int total_count = 0, option_count = 0;
    while (instruments.hasNext()) {
        instruments.next();
        ++total_count;

        if (instruments.instrumentType() == InstrumentType::PERPETUAL) {
            auto exchange_str = trim_null(instruments.exchange(), 8);
            auto underlying_str = trim_null(instruments.underlying(), 24);
            refdata::PerpInstrument pi{
                .instrument_id = instruments.instrumentId(),
                .underlying = underlying_str,
                .exchange = exchange_str,
                .exchange_id = exchange_from_string(exchange_str),
            };
            spdlog::info("[RefdataSubscriber] Perp instrument: {} id={} exchange={}",
                         underlying_str,
                         instruments.instrumentId(),
                         exchange_str);
            if (on_perp_)
                on_perp_(pi);
            continue;
        }

        if (instruments.instrumentType() != InstrumentType::OPTION) {
            spdlog::info("[RefdataSubscriber] Skipping instrument type={} id={}",
                         static_cast<int>(instruments.instrumentType()),
                         instruments.instrumentId());
            continue;
        }
        ++option_count;

        auto exchange_str = trim_null(instruments.exchange(), 8);
        auto underlying_str = trim_null(instruments.underlying(), 24);

        surface::OptionInstrument oi{
            .instrument_id = instruments.instrumentId(),
            .underlying = underlying_str,
            .exchange = exchange_str,
            .exchange_id = exchange_from_string(exchange_str),
            .expiry_date = instruments.expiryDate(),
            .strike_price = instruments.strikePrice(),
            .is_call = (instruments.optionSide() == OptionSide::CALL),
        };

        if (on_option_)
            on_option_(oi);
    }
    spdlog::info("[RefdataSubscriber] Snapshot: {} total instruments, {} options", total_count, option_count);
}

void RefdataSubscriber::on_delta_fragment(const aeron::concurrent::AtomicBuffer& buffer,
                                          aeron::util::index_t offset,
                                          aeron::util::index_t length,
                                          const aeron::Header& /*header*/) {
    using namespace bifrost::protocol;

    if (length < static_cast<aeron::util::index_t>(MessageHeader::encodedLength()))
        return;

    MessageHeader hdr;
    hdr.wrap(const_cast<char*>(reinterpret_cast<const char*>(buffer.buffer() + offset)),
             0,
             MessageHeader::sbeSchemaVersion(),
             static_cast<uint64_t>(length));

    if (hdr.templateId() != RefDataDelta::sbeTemplateId())
        return;

    RefDataDelta delta;
    delta.wrapForDecode(const_cast<char*>(reinterpret_cast<const char*>(buffer.buffer() + offset)),
                        MessageHeader::encodedLength(),
                        hdr.blockLength(),
                        hdr.version(),
                        static_cast<uint64_t>(length));

    if (delta.updateType() == DeltaUpdateType::REMOVE) {
        if (on_remove_)
            on_remove_(delta.instrumentId());
        return;
    }

    // ADD or MODIFY — only care about options
    if (delta.instrumentType() != InstrumentType::OPTION)
        return;

    auto exchange_str = trim_null(delta.exchange(), 8);
    auto underlying_str = trim_null(delta.underlying(), 24);

    surface::OptionInstrument oi{
        .instrument_id = delta.instrumentId(),
        .underlying = underlying_str,
        .exchange = exchange_str,
        .exchange_id = exchange_from_string(exchange_str),
        .expiry_date = delta.expiryDate(),
        .strike_price = delta.strikePrice(),
        .is_call = (delta.optionSide() == OptionSide::CALL),
    };

    if (on_option_)
        on_option_(oi);
}

}  // namespace surtr::refdata

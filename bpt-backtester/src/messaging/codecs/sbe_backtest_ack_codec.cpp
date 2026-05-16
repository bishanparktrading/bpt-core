#include "backtester/messaging/codecs/sbe_backtest_ack_codec.h"

#include <messages/BacktestAck.h>
#include <messages/MessageHeader.h>

#include <cstring>
#include <stdexcept>

namespace bpt::backtester::messaging {

using bpt::messages::BacktestAck;
using bpt::messages::MessageHeader;

std::span<const std::byte> SbeBacktestAckCodec::encode(const BacktestAckMsg& m, std::span<std::byte> scratch) {
    auto* buf = reinterpret_cast<char*>(scratch.data());
    std::memset(buf, 0, scratch.size());

    BacktestAck ack;
    ack.wrapAndApplyHeader(buf, 0, scratch.size()).tickSeqNum(m.tick_seq_num).simulationTs(m.simulation_ts);

    const auto total = MessageHeader::encodedLength() + ack.encodedLength();
    return scratch.subspan(0, total);
}

BacktestAckMsg SbeBacktestAckCodec::decode(std::span<const std::byte> bytes) {
    if (bytes.size() < MessageHeader::encodedLength())
        throw std::runtime_error("SbeBacktestAckCodec::decode: too short");

    auto* data = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
    const uint64_t len = bytes.size();

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), len);

    if (hdr.templateId() != BacktestAck::sbeTemplateId())
        throw std::runtime_error("SbeBacktestAckCodec::decode: wrong template id");

    BacktestAck msg;
    msg.wrapForDecode(data, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), len);

    return BacktestAckMsg{msg.tickSeqNum(), msg.simulationTs()};
}

}  // namespace bpt::backtester::messaging

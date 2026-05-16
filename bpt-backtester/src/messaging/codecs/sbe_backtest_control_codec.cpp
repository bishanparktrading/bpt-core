#include "backtester/messaging/codecs/sbe_backtest_control_codec.h"

#include <messages/BacktestControl.h>
#include <messages/MessageHeader.h>

#include <cstring>
#include <stdexcept>

namespace bpt::backtester::messaging {

using bpt::messages::BacktestControl;
using bpt::messages::MessageHeader;

std::span<const std::byte> SbeBacktestControlCodec::encode(const BacktestControlMsg& m, std::span<std::byte> scratch) {
    auto* buf = reinterpret_cast<char*>(scratch.data());
    std::memset(buf, 0, scratch.size());

    BacktestControl ctrl;
    ctrl.wrapAndApplyHeader(buf, 0, scratch.size())
        .command(m.command)
        .tickSeqNum(m.tick_seq_num)
        .simulationTs(m.simulation_ts);

    const auto total = MessageHeader::encodedLength() + ctrl.encodedLength();
    return scratch.subspan(0, total);
}

BacktestControlMsg SbeBacktestControlCodec::decode(std::span<const std::byte> bytes) {
    if (bytes.size() < MessageHeader::encodedLength())
        throw std::runtime_error("SbeBacktestControlCodec::decode: too short");

    auto* data = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
    const uint64_t len = bytes.size();

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), len);

    if (hdr.templateId() != BacktestControl::sbeTemplateId())
        throw std::runtime_error("SbeBacktestControlCodec::decode: wrong template id");

    BacktestControl msg;
    msg.wrapForDecode(data, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), len);

    return BacktestControlMsg{msg.command(), msg.tickSeqNum(), msg.simulationTs()};
}

}  // namespace bpt::backtester::messaging

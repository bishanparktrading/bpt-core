#include "pms/messaging/codecs/sbe_balance_snapshot_codec.h"

#include <messages/BalanceSnapshot.h>
#include <messages/MessageHeader.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace bpt::pms::messaging {

using bpt::messages::BalanceSnapshot;
using bpt::messages::MessageHeader;

std::span<const std::byte> SbeBalanceSnapshotCodec::encode(const adapter::BalanceSnapshot& snapshot,
                                                           std::span<std::byte> scratch) {
    constexpr std::size_t kMaxRows = 256;
    auto* buf = reinterpret_cast<char*>(scratch.data());
    std::memset(buf, 0, scratch.size());

    BalanceSnapshot msg;
    msg.wrapAndApplyHeader(buf, 0, scratch.size())
        .correlationId(snapshot.correlation_id)
        .timestampNs(snapshot.timestamp_ns);

    const std::size_t n = std::min(snapshot.rows.size(), kMaxRows);
    auto& group = msg.balancesCount(static_cast<uint16_t>(n));
    for (std::size_t i = 0; i < n; ++i) {
        const auto& r = snapshot.rows[i];
        group.next()
            .exchangeId(r.exchange_id)
            .putSubAccount(r.sub_account)
            .putCcy(r.ccy)
            .totalE8(r.total_e8)
            .freeE8(r.free_e8)
            .holdE8(r.hold_e8);
    }

    const auto total = MessageHeader::encodedLength() + msg.encodedLength();
    return scratch.subspan(0, total);
}

adapter::BalanceSnapshot SbeBalanceSnapshotCodec::decode(std::span<const std::byte> bytes) {
    if (bytes.size() < MessageHeader::encodedLength())
        throw std::runtime_error("SbeBalanceSnapshotCodec::decode: bytes too short for MessageHeader");

    auto* data = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
    const uint64_t len = bytes.size();

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), len);

    if (hdr.templateId() != BalanceSnapshot::sbeTemplateId())
        throw std::runtime_error("SbeBalanceSnapshotCodec::decode: wrong template id");

    BalanceSnapshot msg;
    msg.wrapForDecode(data, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), len);

    adapter::BalanceSnapshot out;
    out.correlation_id = msg.correlationId();
    out.timestamp_ns = msg.timestampNs();

    auto& group = msg.balances();
    out.rows.reserve(group.count());
    while (group.hasNext()) {
        group.next();
        adapter::BalanceRow r;
        r.exchange_id = group.exchangeId();
        r.sub_account = group.getSubAccountAsString();
        r.ccy = group.getCcyAsString();
        r.total_e8 = group.totalE8();
        r.free_e8 = group.freeE8();
        r.hold_e8 = group.holdE8();
        out.rows.push_back(std::move(r));
    }

    return out;
}

}  // namespace bpt::pms::messaging

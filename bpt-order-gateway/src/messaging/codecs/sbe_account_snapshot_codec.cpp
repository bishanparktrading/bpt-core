#include "order_gateway/messaging/codecs/sbe_account_snapshot_codec.h"

#include <messages/AccountSnapshot.h>
#include <messages/MessageHeader.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace bpt::order_gateway::messaging {

using bpt::messages::AccountSnapshot;
using bpt::messages::MessageHeader;

std::span<const std::byte> SbeAccountSnapshotCodec::encode(const adapter::AccountSnapshotData& snapshot,
                                                           std::span<std::byte> scratch) {
    constexpr std::size_t kMaxPositions = 500;
    constexpr std::size_t kMaxCurrencyBalances = 32;

    auto* buf = reinterpret_cast<char*>(scratch.data());
    std::memset(buf, 0, scratch.size());

    const std::size_t n_pos = std::min(snapshot.positions.size(), kMaxPositions);
    const std::size_t n_ccy = std::min(snapshot.currency_balances.size(), kMaxCurrencyBalances);

    AccountSnapshot msg;
    msg.wrapAndApplyHeader(buf, 0, scratch.size())
        .exchangeId(snapshot.exchange_id)
        .correlationId(snapshot.correlation_id)
        .timestampNs(snapshot.timestamp_ns)
        .availableBalanceE8(snapshot.available_balance_e8)
        .totalEquityE8(snapshot.total_equity_e8);

    auto& pos_group = msg.positionsCount(static_cast<uint16_t>(n_pos));
    for (std::size_t i = 0; i < n_pos; ++i) {
        const auto& pos = snapshot.positions[i];
        pos_group.next()
            .putExchangeSymbol(pos.exchange_symbol)
            .netQtyE8(pos.net_qty_e8)
            .avgEntryPriceE8(pos.avg_entry_price_e8)
            .unrealizedPnlE8(pos.unrealized_pnl_e8);
    }

    auto& ccy_group = msg.currencyBalancesCount(static_cast<uint16_t>(n_ccy));
    for (std::size_t i = 0; i < n_ccy; ++i) {
        const auto& cb = snapshot.currency_balances[i];
        ccy_group.next().putCcy(cb.ccy).equityE8(cb.equity_e8).availableBalanceE8(cb.available_balance_e8);
    }

    const auto total = MessageHeader::encodedLength() + msg.encodedLength();
    return scratch.subspan(0, total);
}

adapter::AccountSnapshotData SbeAccountSnapshotCodec::decode(std::span<const std::byte> bytes) {
    if (bytes.size() < MessageHeader::encodedLength())
        throw std::runtime_error("SbeAccountSnapshotCodec::decode: too short");

    auto* data = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
    const uint64_t len = bytes.size();

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), len);

    if (hdr.templateId() != AccountSnapshot::sbeTemplateId())
        throw std::runtime_error("SbeAccountSnapshotCodec::decode: wrong template id");

    AccountSnapshot msg;
    msg.wrapForDecode(data, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), len);

    adapter::AccountSnapshotData out;
    out.exchange_id = msg.exchangeId();
    out.correlation_id = msg.correlationId();
    out.timestamp_ns = msg.timestampNs();
    out.available_balance_e8 = msg.availableBalanceE8();
    out.total_equity_e8 = msg.totalEquityE8();

    auto& pos_group = msg.positions();
    out.positions.reserve(pos_group.count());
    while (pos_group.hasNext()) {
        pos_group.next();
        adapter::AccountPosition p;
        p.exchange_symbol = pos_group.getExchangeSymbolAsString();
        p.net_qty_e8 = pos_group.netQtyE8();
        p.avg_entry_price_e8 = pos_group.avgEntryPriceE8();
        p.unrealized_pnl_e8 = pos_group.unrealizedPnlE8();
        out.positions.push_back(std::move(p));
    }

    if (msg.currencyBalancesInActingVersion()) {
        auto& ccy_group = msg.currencyBalances();
        out.currency_balances.reserve(ccy_group.count());
        while (ccy_group.hasNext()) {
            ccy_group.next();
            adapter::CurrencyBalance cb;
            cb.ccy = ccy_group.getCcyAsString();
            cb.equity_e8 = ccy_group.equityE8();
            cb.available_balance_e8 = ccy_group.availableBalanceE8();
            out.currency_balances.push_back(std::move(cb));
        }
    }

    return out;
}

}  // namespace bpt::order_gateway::messaging

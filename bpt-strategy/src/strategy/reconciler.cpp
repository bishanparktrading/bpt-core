#include "strategy/strategy/reconciler.h"

#include "strategy/strategy/position_tracker.h"

#include <cstdlib>

namespace bpt::strategy::strategy {

std::vector<Divergence> reconcile(
    const PositionTracker& tracker,
    bpt::messages::AccountSnapshot& snap,
    const std::unordered_map<uint64_t, std::string>& instrument_id_to_symbol,
    int64_t threshold_e8) {

    std::vector<Divergence> out;

    // Build exchangeSymbol → net_qty_e8 from the snapshot. One pass over
    // the SBE repeating group (non-const — the group carries a read
    // cursor, same pattern as OrderBookState / OFIStrategy).
    std::unordered_map<std::string, int64_t> exchange_by_symbol;
    auto& positions = snap.positions();
    const std::size_t n = positions.count();
    exchange_by_symbol.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        positions.next();
        exchange_by_symbol[positions.getExchangeSymbolAsString()] = positions.netQtyE8();
    }

    const auto exchange_id = snap.exchangeId();

    for (const auto& [instrument_id, symbol] : instrument_id_to_symbol) {
        const int64_t our_qty = tracker.net_qty(instrument_id, exchange_id);
        const auto it = exchange_by_symbol.find(symbol);
        const int64_t exchange_qty = (it == exchange_by_symbol.end()) ? 0 : it->second;
        const int64_t diff = our_qty - exchange_qty;

        if (std::abs(diff) <= threshold_e8) continue;

        out.push_back({
            instrument_id,
            exchange_id,
            (it == exchange_by_symbol.end()) ? std::string{} : symbol,
            our_qty,
            exchange_qty,
            diff,
        });
    }
    return out;
}

}  // namespace bpt::strategy::strategy

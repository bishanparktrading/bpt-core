#pragma once

/// \file
/// \brief OKX MD WS payload builders (pure input → JSON, no state, no I/O).
///
/// Counterpart to OkxMdDecoder (wire → internal); together they bracket
/// the per-venue wire layer in md-gateway.

#include <cstdint>
#include <string>

namespace bpt::md_gateway::adapter::okx {

/// \brief Build a subscribe frame for one instrument.
///
/// \param symbol  exchange-native instId (e.g. "BTC-USDT-SWAP")
/// \param depth   selects the book channel:
///                  - 0  → bbo-tbt (tick-by-tick BBO)
///                  - ≤5 → books5  (top-5 levels)
///                  - >5 → books   (full depth, 400 ms push)
///
/// For SWAP perps, mark-price + index-tickers + funding-rate channels
/// are bundled into the same frame so one round-trip covers book +
/// trades + perp metadata.
[[nodiscard]] std::string build_subscribe_payload(const std::string& symbol, uint8_t depth);

}  // namespace bpt::md_gateway::adapter::okx

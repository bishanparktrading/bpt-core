#pragma once

// OKX market-data WebSocket payload builders — pure input → JSON, no
// state, no I/O. Counterpart to okx_parser (wire → internal); the two
// together bracket the per-exchange wire layer in md-gateway.

#include <cstdint>
#include <string>

namespace bpt::md_gateway::adapter::okx {

// Build a subscribe frame for one instrument. depth selects the book
// channel:
//   0   → bbo-tbt (tick-by-tick BBO)
//   ≤5  → books5  (top-5 levels)
//   >5  → books   (full depth, 400ms push)
// For *-SWAP instruments the funding-rate channel is bundled into the
// same frame so one IO round-trip covers book + trades + funding-rate.
[[nodiscard]] std::string build_subscribe_payload(const std::string& symbol, uint8_t depth);

}  // namespace bpt::md_gateway::adapter::okx

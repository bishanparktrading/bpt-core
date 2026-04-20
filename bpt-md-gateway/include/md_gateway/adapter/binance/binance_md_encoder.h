#pragma once

// Binance market-data WebSocket path builder — pure input → URL query,
// no state, no I/O. Counterpart to binance_parser (wire → internal).
//
// Binance's combined-stream endpoint bakes all subscriptions into the
// URL path at connect time (no runtime subscribe frames), so the only
// outbound encoding is the path's `streams=…` query.

#include "md_gateway/adapter/common/subscription_map.h"

#include <string>

namespace bpt::md_gateway::adapter::binance {

// Return the combined-stream query slug for every symbol in subs, in
// the form expected by wss://stream.binance.com:9443/stream?streams=…:
//   <sym1>@bookTicker/<sym1>@aggTrade/<sym2>@bookTicker/<sym2>@aggTrade/...
// Returns empty string when no subscriptions exist (adapter treats that
// as "nothing to connect to yet, retry later").
[[nodiscard]] std::string build_streams_query(const SubscriptionMap& subs);

}  // namespace bpt::md_gateway::adapter::binance

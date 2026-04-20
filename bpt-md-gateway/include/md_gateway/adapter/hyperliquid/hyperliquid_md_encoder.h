#pragma once

// Hyperliquid market-data JSON builders — pure input → JSON, no state,
// no I/O. Counterpart to hyperliquid_parser (wire → internal).
//
// HL's feed splits by subscription "type": l2Book, trades,
// activeAssetCtx. One subscribe frame per type per coin, bundled by
// the caller.

#include <string>
#include <string_view>

namespace bpt::md_gateway::adapter::hyperliquid {

// Build a subscribe frame for one (sub_type, coin) pair.
// sub_type is one of "l2Book", "trades", "activeAssetCtx".
[[nodiscard]] std::string build_subscribe_payload(std::string_view sub_type, const std::string& coin);

// Application-level ping — HL closes idle sessions ~60s after the last
// client-sent message, so send this on a cadence to keep the connection
// alive through quiet markets.
[[nodiscard]] std::string build_ping_payload();

}  // namespace bpt::md_gateway::adapter::hyperliquid

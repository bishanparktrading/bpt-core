#pragma once

/// \file
/// \brief Hyperliquid MD JSON builders.
///
/// Counterpart to HyperliquidMdDecoder (wire → internal). HL's feed
/// splits by subscription "type" (l2Book, trades, activeAssetCtx) — one
/// subscribe frame per type per coin, bundled by the caller. HL's
/// `subscription` field accepts a single object only; batched array
/// forms return a parse error (verified live).

#include <string>
#include <string_view>

namespace bpt::md_gateway::adapter::hyperliquid {

/// \brief Build a subscribe frame for one (sub_type, coin) pair.
/// \param sub_type one of `"l2Book"`, `"trades"`, `"activeAssetCtx"`
[[nodiscard]] std::string build_subscribe_payload(std::string_view sub_type, const std::string& coin);

/// \brief Application-level ping payload (`{"method":"ping"}`).
///
/// HL closes idle WS sessions ~60 s after the last client-sent message,
/// and WS-level control-frame pings don't reset that timer — only an
/// application message does. RunLoop emits this on a 20 s cadence.
[[nodiscard]] std::string build_ping_payload();

}  // namespace bpt::md_gateway::adapter::hyperliquid

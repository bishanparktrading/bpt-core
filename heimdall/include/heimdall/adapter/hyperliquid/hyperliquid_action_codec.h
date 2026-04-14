#pragma once

// Hyperliquid action JSON builders — pure input → JSON transformation,
// no I/O, no state, no mutex. Each helper produces a boost::json::value
// in exactly the shape Hyperliquid's `/exchange` endpoint (or the
// equivalent WS post action payload) expects.
//
// The returned action object is handed to the signer (which msgpack-
// encodes it and hashes) and then to the transport layer (which wraps
// it with nonce + signature and sends). Callers must NOT mutate the
// action after signing — see HyperliquidSigner for the exact bytes
// contract.

#include <boost/json/fwd.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace heimdall::adapter::hyperliquid {

// Format a double matching Hyperliquid's Python SDK `float_to_wire`:
//   1. Format to 8 decimals ("%.8f")
//   2. Strip trailing zeros after the decimal point
//   3. Strip the decimal point if nothing follows
//
// Examples (must match Python's HL SDK exactly):
//   72198.0575 → "72198.0575"
//   50000.0    → "50000"
//   0.001      → "0.001"
//
// HL's server normalises the wire string to this canonical form before
// hashing, so our msgpack input MUST match byte-for-byte — otherwise
// the server-computed hash diverges from ours and ECDSA recovers a
// garbage address ("User or API Wallet does not exist"). This helper
// is the load-bearing piece that keeps REST and WS order posting
// working on HL testnet.
[[nodiscard]] std::string float_to_wire(double v);

// Per-asset metadata needed to build a well-formed order action.
// TODO(prod): load this from /info meta at startup instead of hardcoding.
// Testnet uses different asset indices than mainnet.
struct AssetMeta {
    int asset_idx;       // HL universe index — what goes into action.orders[].a
    int sz_decimals;     // used to enforce price precision rules
    int max_px_decimals; // 6 - sz_decimals; max decimal places allowed in the price string
};

// Look up AssetMeta for a coin on HL testnet. Returns a sentinel for
// unknown symbols (asset_idx = -1) — callers should reject.
[[nodiscard]] AssetMeta lookup_testnet_asset(std::string_view exchange_symbol);

// Build the `action` JSON for a new order (one-leg limit). Uses
// lookup_testnet_asset for the asset index and float_to_wire + price
// rounding for the wire price/size strings.
//
// price_natural / size_natural are in natural units (e.g. 72108.5 USD,
// 0.001 BTC). The builder rounds the price to satisfy HL's "max 5 sig
// figs / max (6 - szDecimals) decimals" rule for BTC perp, which at
// ~$72k leaves only integer prices as valid. For other assets the
// rounding will need to be per-asset — today it's BTC-only.
[[nodiscard]] boost::json::value build_order_action(std::string_view exchange_symbol,
                                                    bool is_buy,
                                                    double price_natural,
                                                    double size_natural);

// Build the `action` JSON for a cancel-by-exchange-oid. The oid is the
// HL exchange order id from the original `resting` response — NOT our
// internal client order_id.
[[nodiscard]] boost::json::value build_cancel_action(std::string_view exchange_symbol,
                                                     uint64_t exch_oid);

// Build the `action` JSON for a modify. Note: HL does not accept modify
// over the WS `post` endpoint — use REST for this path. Builder is
// transport-agnostic, caller decides.
[[nodiscard]] boost::json::value build_modify_action(std::string_view exchange_symbol,
                                                     uint64_t client_or_exch_oid,
                                                     double price_natural,
                                                     double size_natural);

}  // namespace heimdall::adapter::hyperliquid

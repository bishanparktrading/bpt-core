#pragma once

/// \file
/// \brief Thin wrappers around OpenSSL primitives shared across exchange auth.
///
/// Binance, OKX (order-gateway), and OKX (refdata) each used to carry
/// their own HMAC-SHA256 + base64 implementations. Consolidated here:
/// one `hmac_sha256` returning the raw digest, plus `base64_encode` and
/// `hex_encode` for the two output formats exchanges actually want.
/// Venue auth modules compose these in 1-2 lines.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bpt::common::util {

/// \brief HMAC-SHA256(`key`, `data`) → raw 32-byte digest.
[[nodiscard]] std::vector<uint8_t> hmac_sha256(std::string_view key, std::string_view data);

/// \brief Base64 encode (RFC 4648, single line — no embedded newlines).
[[nodiscard]] std::string base64_encode(const uint8_t* data, std::size_t len);

[[nodiscard]] inline std::string base64_encode(const std::vector<uint8_t>& bytes) {
    return base64_encode(bytes.data(), bytes.size());
}

/// \brief Lowercase hex encoding ("deadbeef…"). Used by Binance signature query string.
[[nodiscard]] std::string hex_encode(const uint8_t* data, std::size_t len);

[[nodiscard]] inline std::string hex_encode(const std::vector<uint8_t>& bytes) {
    return hex_encode(bytes.data(), bytes.size());
}

}  // namespace bpt::common::util

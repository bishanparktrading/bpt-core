#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace heimdall::adapter {

// Signed Hyperliquid transaction — returned by HyperliquidSigner.
struct SignedTransaction {
    std::string r;  // 32-byte hex
    std::string s;  // 32-byte hex
    uint8_t v;      // recovery id
    uint64_t nonce;
};

// Parameters for a new order to sign.
struct OrderSignParams {
    std::string coin;  // exchange native coin (e.g. "BTC")
    bool is_buy;
    double price;
    double size;
    bool reduce_only{false};
    uint64_t cloid;  // client order ID
};

// HyperliquidSigner holds the private key and performs EIP-712 typed data
// signing. SECURITY:
//   - Key is passed in as a 64-char hex string (loaded from Secrets Manager by caller).
//   - Key bytes are stored in a member array zeroed on destruction.
//   - No method exposes the key bytes.
//   - No signing intermediate values are logged.
//   - This class is final, non-copyable, non-movable.
class HyperliquidSigner final {
public:
    // Accepts the 64-char hex private key directly. Throws std::runtime_error if
    // the key is absent or malformed.
    explicit HyperliquidSigner(std::string_view private_key_hex);

    ~HyperliquidSigner();

    HyperliquidSigner(const HyperliquidSigner&) = delete;
    HyperliquidSigner& operator=(const HyperliquidSigner&) = delete;
    HyperliquidSigner(HyperliquidSigner&&) = delete;
    HyperliquidSigner& operator=(HyperliquidSigner&&) = delete;

    // Sign a new order. Returns the signed transaction for inclusion in the REST
    // request.
    [[nodiscard]] SignedTransaction sign_order(const OrderSignParams& params);

    // Sign a cancel request.
    [[nodiscard]] SignedTransaction sign_cancel(const std::string& coin, uint64_t oid);

    // Increment and return the next nonce.
    [[nodiscard]] uint64_t next_nonce() noexcept;

private:
    // 32-byte private key — zeroed in destructor.
    std::array<uint8_t, 32> key_{};
    uint64_t nonce_{0};

    // EIP-712 domain separator (precomputed from HL mainnet chain ID 1337).
    // Implementation uses OpenSSL for keccak256 and secp256k1 for ECDSA.
    [[nodiscard]] std::array<uint8_t, 32> keccak256(const uint8_t* data, std::size_t len) const;
    [[nodiscard]] SignedTransaction ecdsa_sign(const std::array<uint8_t, 32>& hash) const;
};

}  // namespace heimdall::adapter

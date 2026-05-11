#include "order_gateway/adapter/binance/binance_auth.h"

#include <iomanip>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <sstream>
#include <bpt_common/util/tsc_clock.h>

namespace bpt::order_gateway::adapter::binance {

using bpt::common::util::WallClock;

std::string hmac_sha256_hex(std::string_view key, std::string_view data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(),
         key.data(),
         static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()),
         static_cast<int>(data.size()),
         digest,
         &digest_len);

    std::ostringstream oss;
    for (unsigned int i = 0; i < digest_len; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    return oss.str();
}

std::string sign_query(std::string_view secret_key, const std::string& params) {
    const uint64_t ts_ms = WallClock::now_ms();
    std::string full = params + "&timestamp=" + std::to_string(ts_ms);
    std::string sig = hmac_sha256_hex(secret_key, full);
    return full + "&signature=" + sig;
}

}  // namespace bpt::order_gateway::adapter::binance

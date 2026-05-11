#pragma once

/// \file
/// \brief ASCII string-case helpers shared across services.
///
/// Three near-identical `lowercase_venue` copies existed in
/// bpt-md-gateway, bpt-order-gateway, and bpt-tape before this header;
/// callers now share these definitions. Operates on ASCII via
/// `std::tolower`/`std::toupper`; bytes outside [0,127] are passed
/// through unchanged on most platforms but the result is not
/// locale-aware — matches the venue-naming use case (BTC, OKX, etc.).

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace bpt::common::util {

[[nodiscard]] inline std::string to_lower(std::string_view s) {
    std::string out{s};
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

[[nodiscard]] inline std::string to_upper(std::string_view s) {
    std::string out{s};
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return out;
}

}  // namespace bpt::common::util

#pragma once

/// \file
/// \brief Role-qualified service name helper — same convention across services.
///
/// Each main.cpp had its own near-identical copy that produced strings
/// like "bpt-rfd-hyperliquid", "bpt-mdgw-hyperliquid", "bpt-ogw-okx",
/// "bpt-bridge-hyperliquid" — used for Quill logger identity, journal
/// brackets in log lines, and (occasionally) systemd unit names. The
/// suffix appears only when the service is bound to a single venue so
/// per-instance logs stay greppable when multiple instances of the
/// same service run on one host (one per venue).
///
/// Multi-venue services (e.g. md-gateway configured with
/// `exchanges = ["OKX", "BINANCE"]`) get the unsuffixed form
/// "bpt-mdgw" — the venue isn't a useful discriminator when more than
/// one is active.
///
/// `bpt-strategy` keeps its own derive function because its
/// discriminator is the strategy CLASS (`AvellanedaStoikov` → `as`,
/// `FundingArb` → `farb`), not the venue.

#include "bpt_common/util/strings.h"

#include <string>
#include <string_view>
#include <vector>

namespace bpt::common::util {

/// \brief Compose `"bpt-<role>"` (multi-venue) or `"bpt-<role>-<venue>"`
///        (single-venue) for logging / journal identity.
///
/// `role` is the short service token: "rfd", "mdgw", "ogw", "bridge",
/// "tape", "book", etc.
///
/// `enabled_venues` is the list of currently-active venue names. The
/// caller pre-filters (e.g. by `AdapterConfig::enabled` flag) — this
/// helper just inspects the resulting count.
///
/// Examples:
///   derive_service_name("rfd",    {"HYPERLIQUID"})        → "bpt-rfd-hyperliquid"
///   derive_service_name("mdgw",   {"OKX", "BINANCE"})     → "bpt-mdgw"
///   derive_service_name("bridge", {})                     → "bpt-bridge"
[[nodiscard]] inline std::string derive_service_name(std::string_view role,
                                                     const std::vector<std::string>& enabled_venues) {
    std::string name = "bpt-";
    name += role;
    if (enabled_venues.size() == 1) {
        name += "-";
        name += to_lower(enabled_venues[0]);
    }
    return name;
}

}  // namespace bpt::common::util

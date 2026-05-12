#pragma once

/// \file
/// \brief Shared deployment-profile config loaded from `deploy/config/profile/<tag>.toml`.
///
/// The trading stack used to declare `environment`, `exchanges`, and
/// `exchange_config` separately in every service's TOML. The defence
/// against drift was naming convention: pick `*.qa-hyperliquid.toml`
/// for each service and hope they agree. Nothing structural stopped
/// `bpt-refdata.qa-hyperliquid.toml` from saying `exchanges=["OKX"]`
/// while `bpt-md-gateway.qa-hyperliquid.toml` said `["HYPERLIQUID"]`.
///
/// Now every service references the same shared file via its config:
///
///   profile_config = "deploy/config/profile/qa-hyperliquid.toml"
///
/// and the loader copies the profile's `environment`/`exchanges`/
/// `exchange_config` into the service's settings. Per-TOML fields are
/// honoured as overrides (transition path); once all services migrate,
/// the duplicate fields can be dropped from per-service TOMLs.
///
/// Format of `<tag>.toml`:
///
///   environment     = "qa"                    # dev / qa / prod
///   exchanges       = ["HYPERLIQUID"]
///   exchange_config = "config/exchanges/testnet.toml"   # optional;
///                                             # service-relative path
///

#include "bpt_common/env.h"

#include <string>
#include <vector>

namespace bpt::common::config {

/// \brief Parsed contents of a profile TOML.
struct ProfileConfig {
    /// `environment = "..."` — dev / qa / prod. Validated by env_from_string.
    bpt::common::Env environment{bpt::common::Env::DEV};

    /// `exchanges = [...]` — active venue filter applied across the stack.
    std::vector<std::string> exchanges;

    /// `exchange_config = "..."` — service-relative path to the per-env
    /// exchange-endpoints file (e.g. `config/exchanges/testnet.toml`).
    /// Optional: backtest profiles omit it because each service has its
    /// own backtest exchange-config file with a distinct name.
    std::string exchange_config;
};

/// \brief Parse a profile TOML. Throws `std::runtime_error` on parse
///        failure (malformed TOML, file not found, invalid environment).
[[nodiscard]] ProfileConfig load_profile_config(const std::string& path);

}  // namespace bpt::common::config

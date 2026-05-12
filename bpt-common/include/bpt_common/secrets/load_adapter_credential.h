#pragma once

/// \file
/// \brief Strict-mode policy for adapter credential loading — shared across services.
///
/// `bpt-order-gateway` and `bpt-refdata` each iterate over their own
/// `AdapterConfig` list and need to load systemd-creds per adapter.
/// The credential and AdapterConfig types are namespace-disjoint (each
/// service owns its own shape), but the strict / dev / fetch policy is
/// identical. This helper extracts that one policy so it lives in one
/// place — the iteration / filter / post-step stays at the call site
/// where the per-service differences (refdata's `enabled` filter,
/// order-gateway's backtest `wallet_address` override) remain visible.

#include "bpt_common/env.h"
#include "bpt_common/logging.h"
#include "bpt_common/secrets/secrets_client.h"

#include <fmt/format.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace bpt::common::secrets {

/// \brief Loads one adapter's credentials with the shared strict-mode policy.
///
/// Policy:
///   - `secret_name` empty + env in {QA, PROD} → throw (deploy bug)
///   - `secret_name` empty + dev               → warn, return default-constructed Creds
///   - `secret_name` set                       → fetch, convert via from_secret(), info-log
///
/// `from_secret` is the service-specific factory (e.g.
/// `bpt::order_gateway::adapter::credentials_from_secret`) — the
/// service owns the credential type; this helper just runs the policy.
template <class Creds, class FromSecret>
Creds load_adapter_credential(std::string_view exchange,
                              std::string_view secret_name,
                              bpt::common::Env env,
                              FromSecret&& from_secret) {
    const bool strict = (env == bpt::common::Env::QA || env == bpt::common::Env::PROD);
    if (secret_name.empty()) {
        if (strict)
            throw std::runtime_error(fmt::format("env={} but adapter {} has empty secret_name — refusing to start",
                                                 bpt::common::to_string(env),
                                                 exchange));
        bpt::common::log::warn("No secret_name for {} — adapter will have empty credentials (dev only)", exchange);
        return Creds{};
    }
    const auto kv = bpt::common::secrets::fetch(std::string(secret_name), env);
    Creds c = std::forward<FromSecret>(from_secret)(std::string(exchange), kv);
    bpt::common::log::info("Loaded credentials for {}", exchange);
    return c;
}

}  // namespace bpt::common::secrets

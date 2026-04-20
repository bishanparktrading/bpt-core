#pragma once

// Deployment-environment label. Process-level runtime identity; used by
// secrets_client to choose strict vs permissive credential delivery, by
// bpt-app's BaseSettings for lifecycle knobs (prod banner, validation),
// and by any other primitive that wants to branch on environment.
//
// Strictly validated — env_from_string() rejects anything outside
// {"dev","qa","prod"} including empty and any case variation. Catches
// typos like "prd" that would otherwise silently skip every is_prod()
// check in the codebase.

#include <string_view>

namespace bpt::common {

enum class Env {
    DEV,   // local / laptop / backtest
    QA,    // staging / testnet / paper
    PROD,  // live capital
};

// Lowercase form — matches TOML value, Prometheus label conventions.
[[nodiscard]] std::string_view to_string(Env e) noexcept;

// Inverse of to_string. Throws std::runtime_error on unknown values.
[[nodiscard]] Env env_from_string(std::string_view s);

}  // namespace bpt::common

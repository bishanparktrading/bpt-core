#pragma once

/// \file
/// \brief Latency model abstractions for backtest fill timing.

#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>

namespace bpt::backtester::latency {

/// \brief Latency leg.
///
/// submit_to_match: time between an order leaving the gateway
/// and the exchange matching engine considering it. match_to_report: time
/// between the matching engine producing an exec report and the strategy
/// receiving it. Both are observed in production telemetry; modelling them
/// separately lets a backtest reproduce the asymmetry where order placement
/// is sometimes faster than fill notification (or the other way around).
enum class LatencyLeg {
    SUBMIT_TO_MATCH,
    MATCH_TO_REPORT,
};

/// \brief Abstract latency model.
///
/// Strategies that need different distributions per venue, per order type,
/// or per book state subclass this and override draw.
class LatencyModel {
public:
    virtual ~LatencyModel() = default;

    /// \brief Returns a single nanosecond delay sample for the given (venue, leg).
    ///
    /// Implementations must be deterministic given a fixed seed — sweeps and
    /// reproducibility rely on it.
    /// \param venue Wire-format exchange name.
    /// \param leg   Which latency leg to sample.
    /// \return Nanoseconds delay.
    virtual uint64_t draw(const std::string& venue, LatencyLeg leg) = 0;
};

/// \brief Parametric model: per-(venue, leg) base_ns + uniform[0, jitter_ns) draw,
///        seeded once.
///
/// The simplest model that's still useful — captures the constant component
/// (network + gateway floor) and a coarse first-order noise term. Empirical-
/// distribution models go in a sibling header once production telemetry is
/// mineable.
class ParametricLatencyModel final : public LatencyModel {
public:
    struct Spec {
        uint64_t base_ns{0};
        uint64_t jitter_ns{0};  ///< uniform [0, jitter_ns).
    };

    /// \brief Per-venue, per-leg spec.
    ///
    /// Lookup is by exact venue string match ("BINANCE", "OKX", "HYPERLIQUID",
    /// "DERIBIT") — the same uppercase form used everywhere else in the
    /// codebase. Unknown venues fall back to default_spec_ which defaults to
    /// {0, 0}.
    explicit ParametricLatencyModel(uint64_t seed) : rng_(seed) {}

    void set_spec(const std::string& venue, LatencyLeg leg, Spec spec) { specs_[key(venue, leg)] = spec; }

    void set_default(LatencyLeg leg, Spec spec) { default_specs_[static_cast<std::size_t>(leg)] = spec; }

    uint64_t draw(const std::string& venue, LatencyLeg leg) override {
        const auto it = specs_.find(key(venue, leg));
        const Spec& s = (it != specs_.end()) ? it->second : default_specs_[static_cast<std::size_t>(leg)];
        if (s.jitter_ns == 0)
            return s.base_ns;
        std::uniform_int_distribution<uint64_t> dist(0, s.jitter_ns - 1);
        return s.base_ns + dist(rng_);
    }

private:
    static uint64_t key(const std::string& venue, LatencyLeg leg) {
        // Stable hash of (venue, leg) for unordered_map key. We avoid
        // std::hash<string> + leg to keep the determinism contract on the
        // map itself irrelevant — only the *draw output* needs to be
        // deterministic for a fixed seed.
        uint64_t h = 14695981039346656037ULL;  // FNV-1a offset basis
        for (char c : venue) {
            h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
            h *= 1099511628211ULL;
        }
        h ^= static_cast<uint64_t>(leg);
        h *= 1099511628211ULL;
        return h;
    }

    std::mt19937_64 rng_;
    std::unordered_map<uint64_t, Spec> specs_;
    Spec default_specs_[2]{};  ///< indexed by LatencyLeg.
};

}  // namespace bpt::backtester::latency

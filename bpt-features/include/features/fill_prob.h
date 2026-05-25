#pragma once

// Fill-probability models for resting maker orders.
//
// Two formulations are useful for different contexts. Both take a
// `queue_ahead` estimate (volume resting at-or-better than our price
// that must be cleared before our order fills) and return a number
// in [0, 1]. Higher = more likely to fill.
//
// Stateless / pure functions — callers wire the inputs from wherever
// state lives (KappaEstimator for κ, OrderBookState for queue_ahead).

#include <cmath>

namespace bpt::features {

// Poisson-arrival fill probability: P(fill within horizon_s) given that
// taker volume arrives at our price as a Poisson process at rate kappa
// (trades/sec). Our first unit fills when cumulative arrived volume
// crosses queue_ahead, so the expected wait is queue_ahead / kappa.
//
//     P ≈ 1 - exp(-kappa * horizon_s / queue_ahead)
//
// Returns 1.0 when queue_ahead <= 0 (front of queue, certain fill).
// Returns 0.0 when kappa <= 0 (dead market) or horizon_s <= 0.
//
// Assumes constant trade size (queue_ahead and kappa in the same units).
// FIFO queue. Doesn't model silent cancels of orders ahead, so the
// estimate is biased high relative to reality where some queue_ahead
// disappears without a public trade.
inline double fill_probability_poisson(double kappa, double horizon_s, double queue_ahead) {
    if (queue_ahead <= 0.0) {
        return 1.0;
    }
    if (kappa <= 0.0 || horizon_s <= 0.0) {
        return 0.0;
    }
    return 1.0 - std::exp(-kappa * horizon_s / queue_ahead);
}

// Geometric / ordinal fill share: our_qty / (our_qty + queue_ahead).
// Pure ratio, no time dimension. Useful as a fallback when kappa hasn't
// warmed up, or as a unit-free ranking signal.
//
// Returns 1.0 when queue_ahead <= 0. Returns 0.0 when our_qty <= 0.
//
// NOT a probability — it's the share of total resting size at our price
// that's ours. Goes to 1 as queue_ahead → 0, to 0 as queue_ahead → ∞,
// and 0.5 when our_qty == queue_ahead. The mapping to true fill
// probability requires an arrival-rate assumption, which the Poisson
// form above makes explicit.
inline double fill_probability_geometric(double our_qty, double queue_ahead) {
    if (queue_ahead <= 0.0) {
        return 1.0;
    }
    if (our_qty <= 0.0) {
        return 0.0;
    }
    return our_qty / (our_qty + queue_ahead);
}

}  // namespace bpt::features

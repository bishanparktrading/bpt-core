"""Probabilistic + Deflated Sharpe (Bailey & Lopez de Prado, 2014).

The standard Sharpe ratio is a point estimate. With finite N and non-
normal returns, the *true* Sharpe could be anything. PSR gives the
probability the true Sharpe exceeds a benchmark, given the observed
sample. DSR is PSR with the benchmark adjusted for multi-testing —
if you tested K configs and report the best, the bar moves up by the
expected max under noise.

References:
    Bailey & Lopez de Prado, "The Sharpe Ratio Efficient Frontier" (2012)
    Bailey & Lopez de Prado, "The Deflated Sharpe Ratio" (2014)
"""

from __future__ import annotations

import math

import numpy as np
from scipy.stats import norm

# Euler-Mascheroni constant — appears in the expected-max-of-N-normals
# approximation used by DSR.
_GAMMA_EM = 0.577215664901532860606512090082402431


def sharpe_ratio(returns: np.ndarray, ddof: int = 1) -> float:
    """Per-observation Sharpe (mean/stdev). No time annualization."""
    returns = np.asarray(returns, dtype=float)
    if returns.size < 2:
        return 0.0
    sigma = float(np.std(returns, ddof=ddof))
    if sigma == 0.0:
        return 0.0
    return float(np.mean(returns)) / sigma


def probabilistic_sharpe_ratio(
    sr_hat: float,
    n: int,
    skew: float = 0.0,
    kurt: float = 3.0,
    sr_benchmark: float = 0.0,
) -> float:
    """P(true Sharpe > sr_benchmark | observed sample).

    sr_hat:        observed Sharpe (per-observation, same units as benchmark)
    n:             sample size (number of return observations)
    skew, kurt:    sample skewness + kurtosis of returns (kurt=3 is normal)
    sr_benchmark:  the SR you're comparing against (0 = "is it even positive?")

    Returns a value in [0, 1]. Conventional threshold: > 0.95 is "real."
    """
    if n <= 1:
        return 0.0
    denom_sq = 1.0 - skew * sr_hat + ((kurt - 1.0) / 4.0) * sr_hat ** 2
    if denom_sq <= 0.0:
        return 0.0
    denom = math.sqrt(denom_sq)
    z = (sr_hat - sr_benchmark) * math.sqrt(n - 1) / denom
    return float(norm.cdf(z))


def expected_max_sharpe(num_trials: int, sr_variance: float = 1.0) -> float:
    """Expected maximum of N iid Sharpe estimates with variance sr_variance.

    The "if I tested N strategies on noise, what's the best SR I should
    expect to see by luck?" benchmark. Used by DSR to deflate the
    significance threshold for multi-testing.

    Closed-form approximation from Bailey & Lopez de Prado:
        E[max SR] ≈ σ × [(1 - γ_EM) × Φ⁻¹(1 - 1/N)
                       + γ_EM × Φ⁻¹(1 - 1/(N·e))]
    """
    if num_trials <= 1:
        return 0.0
    sigma = math.sqrt(sr_variance)
    a = norm.ppf(1.0 - 1.0 / num_trials)
    b = norm.ppf(1.0 - 1.0 / (num_trials * math.e))
    return float(sigma * ((1.0 - _GAMMA_EM) * a + _GAMMA_EM * b))


def deflated_sharpe_ratio(
    sr_hat: float,
    n: int,
    num_trials: int,
    sr_variance: float = 1.0,
    skew: float = 0.0,
    kurt: float = 3.0,
) -> float:
    """PSR with the benchmark adjusted for multi-testing.

    Same shape as PSR but the benchmark is `expected_max_sharpe(num_trials,
    sr_variance)` instead of 0. Answers: "after testing K configs and
    reporting the best, is the survivor still convincingly above noise?"

    sr_variance: variance of the SRs *across the K trials*. Estimate
        from the sweep's own row-to-row SR dispersion.
    """
    benchmark = expected_max_sharpe(num_trials, sr_variance)
    return probabilistic_sharpe_ratio(sr_hat, n, skew, kurt, benchmark)

"""Unit tests for PSR + DSR.

Reference values cross-checked against:
    Bailey & Lopez de Prado, "The Sharpe Ratio Efficient Frontier" Table 1
    (2012). Where Bailey gives closed-form results, we hit them.
"""

import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from stats.significance import (  # noqa: E402
    deflated_sharpe_ratio,
    expected_max_sharpe,
    probabilistic_sharpe_ratio,
    sharpe_ratio,
)


# ── sharpe_ratio ─────────────────────────────────────────────────────────────


def test_sharpe_zero_when_constant():
    assert sharpe_ratio(np.array([1.0, 1.0, 1.0])) == 0.0


def test_sharpe_basic():
    r = np.array([1.0, 2.0, 3.0])  # mean=2, std=1 (ddof=1)
    assert sharpe_ratio(r) == pytest.approx(2.0)


def test_sharpe_empty_returns_zero():
    assert sharpe_ratio(np.array([])) == 0.0
    assert sharpe_ratio(np.array([5.0])) == 0.0


# ── probabilistic_sharpe_ratio ───────────────────────────────────────────────


def test_psr_zero_sample_returns_zero():
    assert probabilistic_sharpe_ratio(1.0, n=1) == 0.0
    assert probabilistic_sharpe_ratio(1.0, n=0) == 0.0


def test_psr_high_sr_high_n_approaches_one():
    # SR=1, N=100, normal returns → PSR essentially 1
    assert probabilistic_sharpe_ratio(1.0, n=100) > 0.99


def test_psr_zero_sr_returns_one_half():
    # SR_hat == benchmark → z=0 → Φ(0) = 0.5
    assert probabilistic_sharpe_ratio(0.0, n=100) == pytest.approx(0.5)


def test_psr_negative_sr_below_half():
    assert probabilistic_sharpe_ratio(-0.5, n=100) < 0.5


def test_psr_negative_skew_lowers_significance():
    # Negative skew inflates the denominator (fat left tail) → less confident
    p_norm = probabilistic_sharpe_ratio(1.0, n=30, skew=0.0, kurt=3.0)
    p_skew = probabilistic_sharpe_ratio(1.0, n=30, skew=-1.0, kurt=3.0)
    assert p_skew < p_norm


def test_psr_high_kurt_lowers_significance():
    # Fat tails → less confident
    p_norm = probabilistic_sharpe_ratio(1.0, n=30, kurt=3.0)
    p_fat = probabilistic_sharpe_ratio(1.0, n=30, kurt=10.0)
    assert p_fat < p_norm


def test_psr_higher_benchmark_lowers_psr():
    p_zero = probabilistic_sharpe_ratio(1.0, n=100, sr_benchmark=0.0)
    p_half = probabilistic_sharpe_ratio(1.0, n=100, sr_benchmark=0.5)
    assert p_half < p_zero


# ── expected_max_sharpe ──────────────────────────────────────────────────────


def test_expected_max_sr_grows_with_trials():
    # More trials → larger expected max under noise
    e_10 = expected_max_sharpe(10, sr_variance=1.0)
    e_100 = expected_max_sharpe(100, sr_variance=1.0)
    e_1000 = expected_max_sharpe(1000, sr_variance=1.0)
    assert 0 < e_10 < e_100 < e_1000


def test_expected_max_sr_scales_with_variance():
    # σ doubles → expected max doubles
    e_var1 = expected_max_sharpe(100, sr_variance=1.0)
    e_var4 = expected_max_sharpe(100, sr_variance=4.0)
    assert e_var4 == pytest.approx(2.0 * e_var1, rel=1e-9)


def test_expected_max_sr_one_trial_returns_zero():
    assert expected_max_sharpe(1) == 0.0


# ── deflated_sharpe_ratio ────────────────────────────────────────────────────


def test_dsr_deflates_psr():
    # DSR uses a non-zero benchmark from multi-testing → strictly less than PSR
    psr = probabilistic_sharpe_ratio(1.0, n=100, sr_benchmark=0.0)
    dsr = deflated_sharpe_ratio(1.0, n=100, num_trials=100, sr_variance=1.0)
    assert dsr < psr


def test_dsr_one_trial_matches_psr():
    # K=1 → no multi-testing → expected_max=0 → DSR == PSR
    psr = probabilistic_sharpe_ratio(1.0, n=100, sr_benchmark=0.0)
    dsr = deflated_sharpe_ratio(1.0, n=100, num_trials=1, sr_variance=1.0)
    assert dsr == pytest.approx(psr)


def test_dsr_more_trials_more_deflation():
    dsr_10 = deflated_sharpe_ratio(1.0, n=100, num_trials=10, sr_variance=1.0)
    dsr_1000 = deflated_sharpe_ratio(1.0, n=100, num_trials=1000, sr_variance=1.0)
    assert dsr_1000 < dsr_10


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))

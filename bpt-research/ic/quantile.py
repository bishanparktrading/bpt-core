"""Quantile / decile analysis — monotonicity check on a signal.

IC=0.20 can hide a non-monotone signal-return relationship (e.g. a
U-shape) that's untradeable. The fix: bin ticks by signal quantile,
compute mean forward return per bin, and check the bins step
monotonically. This is what production scorecards live and die on
before any backtest gets run.
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable

import numpy as np
import pandas as pd

import bpt_canon as bc

from .panel import _prepare_bbo, _forward_return

FeatureFn = Callable[[pd.DataFrame], pd.Series]


def quantile_analysis(
    canon_paths: list[Path],
    feature_fn: FeatureFn,
    *,
    n_bins: int = 10,
    horizon_ns: int = 1_000_000_000,
    min_ticks_per_cell: int = 500,
    pool_across_days: bool = True,
) -> pd.DataFrame:
    """Mean forward return per signal quantile.

    Args:
        canon_paths:     canon files
        feature_fn:      callable(bbo_df) -> Series, the signal under test
        n_bins:          deciles (10), quintiles (5), etc.
        horizon_ns:      forward-return horizon (default 1s)
        min_ticks_per_cell: drop (day, instrument) cells with too few rows
        pool_across_days:  if True, return one row per (instrument, bin)
                           with stats pooled across all days; if False,
                           one row per (day, instrument, bin)

    Returns:
        DataFrame with columns: instrument_id [, day], bin, n,
        mean_ret_bps, std_ret_bps, signal_low, signal_high.

        bin runs 1..n_bins (1 = lowest signal, n_bins = highest). A
        monotone signal shows mean_ret_bps stepping up (or down) across
        bins; a flat or U-shaped pattern is the warning sign.
    """
    rows = []
    for cp in canon_paths:
        day = cp.stem
        bbo = _prepare_bbo(bc.read_bbos(cp))
        for iid, grp in bbo.groupby("instrument_id", sort=True):
            grp = grp.reset_index(drop=True)
            ret = _forward_return(grp, horizon_ns)
            sig = feature_fn(grp)
            mask = ret.notna() & sig.notna()
            n = int(mask.sum())
            if n < min_ticks_per_cell:
                continue
            sig_v = sig[mask].values
            ret_v = ret[mask].values
            try:
                bin_idx = pd.qcut(sig_v, n_bins, labels=False, duplicates="drop")
            except ValueError:
                # signal degenerate (all same value) — can't bin
                continue
            df = pd.DataFrame({"bin": bin_idx + 1, "sig": sig_v, "ret": ret_v})
            agg = df.groupby("bin").agg(
                n=("ret", "size"),
                mean_ret=("ret", "mean"),
                std_ret=("ret", "std"),
                signal_low=("sig", "min"),
                signal_high=("sig", "max"),
            ).reset_index()
            agg["day"] = day
            agg["instrument_id"] = int(iid)
            rows.append(agg)

    if not rows:
        return pd.DataFrame()
    out = pd.concat(rows, ignore_index=True)
    out["mean_ret_bps"] = out.mean_ret * 1e4
    out["std_ret_bps"] = out.std_ret * 1e4
    out = out.drop(columns=["mean_ret", "std_ret"])

    if pool_across_days:
        # Reaggregate across days. Re-bin not needed — we treat each day's
        # already-binned cells as independent samples per bin and average.
        pooled = out.groupby(["instrument_id", "bin"]).agg(
            n_days=("day", "nunique"),
            n=("n", "sum"),
            mean_ret_bps=("mean_ret_bps", "mean"),
            std_ret_bps=("std_ret_bps", "mean"),
            signal_low=("signal_low", "min"),
            signal_high=("signal_high", "max"),
        ).reset_index()
        return pooled
    return out[["day", "instrument_id", "bin", "n",
                "mean_ret_bps", "std_ret_bps", "signal_low", "signal_high"]]


def monotonicity_score(
    quantile_df: pd.DataFrame,
    *,
    min_bins: int = 6,
) -> pd.DataFrame:
    """Per-instrument: how monotone is the bin → return relationship?

    Returns Spearman rank correlation between `bin` and `mean_ret_bps`
    per instrument. ≈+1 = clean upward steps. ≈−1 = downward. ≈0 =
    non-monotone (U-shape, noise, etc.) — the failure mode IC can hide.

    `min_bins`: instruments with fewer surviving bins are emitted with
    NaN monotonicity rather than a spurious score. With only 3 bins,
    monotone-by-chance is non-trivial; the default 6 matches the
    typical "deciles minus a couple of duplicates-dropped" floor.
    """
    from scipy.stats import spearmanr

    rows = []
    for iid, grp in quantile_df.groupby("instrument_id"):
        grp = grp.sort_values("bin")
        n_bins = len(grp)
        if n_bins < min_bins:
            rows.append({"instrument_id": int(iid),
                         "monotonicity": np.nan,
                         "n_bins": n_bins})
            continue
        rho, _ = spearmanr(grp.bin, grp.mean_ret_bps)
        rows.append({"instrument_id": int(iid),
                     "monotonicity": float(rho),
                     "n_bins": n_bins})
    return pd.DataFrame(rows).sort_values("monotonicity", ascending=False)

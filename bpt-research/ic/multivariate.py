"""Multivariate IC tools — partial slopes, feature correlation,
and composite-signal construction.

Two-feature OFI study showed: independent ICs aren't redundant
when the cross-feature correlation is moderate (+0.33 for OFI vs
microprice_dev). This module handles the next questions:

1. **Partial slope** — fit `ret ~ f1 + f2 + ...` per (instrument, day),
   read off each feature's *unique* contribution. Disentangles redundancy.
2. **Feature correlation** — pairwise Spearman matrix between features
   pooled across cells. Tells you which features overlap.
3. **Composite signal** — z-score features per cell, combine with fitted
   OLS weights. Compatible with `ic_panel` as a normal feature_fn so
   the composite gets the same evaluation harness as raw features.

Walk-forward train/test splitting is intentionally NOT inside `fit_composite_weights`
— the caller picks the canon subsets for train vs test. Keeps the
function honest about what data it saw.
"""

from __future__ import annotations

from itertools import combinations
from pathlib import Path
from typing import Callable

import numpy as np
import pandas as pd
from scipy.stats import spearmanr

import bpt_canon as bc

from .panel import _prepare_bbo, _forward_return

FeatureFn = Callable[[pd.DataFrame], pd.Series]


def _z(x: np.ndarray) -> np.ndarray:
    """Standardize: (x - mean) / std. Safe on degenerate (std=0) input."""
    mu = np.nanmean(x)
    sd = np.nanstd(x)
    if sd == 0 or np.isnan(sd):
        return np.zeros_like(x)
    return (x - mu) / sd


def partial_slopes(
    canon_paths: list[Path],
    features: dict[str, FeatureFn],
    *,
    horizon_ns: int = 1_000_000_000,
    min_ticks: int = 500,
) -> pd.DataFrame:
    """One row per (day, instrument, feature) with the partial OLS slope.

    Fits `ret = const + sum_i beta_i * feature_i` per cell via least
    squares; reports each feature's beta. Compare to `ic_panel`'s
    `beta_uni` (univariate slope) to see how much each feature's
    contribution gets revised when others are controlled for.
    """
    rows = []
    feat_names = list(features.keys())
    for cp in canon_paths:
        day = cp.stem
        bbo = _prepare_bbo(bc.read_bbos(cp))
        for iid, grp in bbo.groupby("instrument_id", sort=True):
            grp = grp.reset_index(drop=True)
            ret = _forward_return(grp, horizon_ns).values
            cols = {name: fn(grp).values for name, fn in features.items()}
            mat = np.column_stack([cols[n] for n in feat_names])
            valid = (
                ~np.isnan(ret) & ~np.any(np.isnan(mat), axis=1)
            )
            n = int(valid.sum())
            if n < min_ticks:
                for name in feat_names:
                    rows.append({
                        "day": day, "instrument_id": int(iid),
                        "feature": name, "n": n,
                        "beta_partial": np.nan,
                    })
                continue
            X = np.column_stack([mat[valid], np.ones(n)])
            coef, *_ = np.linalg.lstsq(X, ret[valid], rcond=None)
            for i, name in enumerate(feat_names):
                rows.append({
                    "day": day, "instrument_id": int(iid),
                    "feature": name, "n": n,
                    "beta_partial": float(coef[i]),
                })
    return pd.DataFrame(rows)


def feature_correlation(
    canon_paths: list[Path],
    features: dict[str, FeatureFn],
    *,
    min_ticks: int = 500,
    pool_across_cells: bool = True,
) -> pd.DataFrame:
    """Pairwise Spearman correlation between features.

    `pool_across_cells=True`: average correlation across cells, one row
    per (feature_a, feature_b). The headline view.

    `pool_across_cells=False`: one row per (day, instrument, feature_a,
    feature_b) — useful for instrument-level redundancy maps.
    """
    rows = []
    pairs = list(combinations(features.keys(), 2))
    for cp in canon_paths:
        day = cp.stem
        bbo = _prepare_bbo(bc.read_bbos(cp))
        for iid, grp in bbo.groupby("instrument_id", sort=True):
            grp = grp.reset_index(drop=True)
            cached = {name: fn(grp).values for name, fn in features.items()}
            for a, b in pairs:
                va, vb = cached[a], cached[b]
                valid = ~np.isnan(va) & ~np.isnan(vb)
                if int(valid.sum()) < min_ticks:
                    continue
                rho, _ = spearmanr(va[valid], vb[valid])
                rows.append({
                    "day": day, "instrument_id": int(iid),
                    "feature_a": a, "feature_b": b,
                    "corr": float(rho),
                    "n": int(valid.sum()),
                })
    df = pd.DataFrame(rows)
    if not pool_across_cells:
        return df
    return df.groupby(["feature_a", "feature_b"]).agg(
        corr_mean=("corr", "mean"),
        corr_std=("corr", "std"),
        cells=("corr", "size"),
    ).reset_index().sort_values("corr_mean", key=abs, ascending=False)


def fit_composite_weights(
    canon_paths: list[Path],
    features: dict[str, FeatureFn],
    *,
    horizon_ns: int = 1_000_000_000,
    min_ticks: int = 500,
) -> dict[str, float]:
    """Pool all valid (day, instrument) ticks, z-score features per cell,
    fit `ret ~ z1 + z2 + ...` once, return `{feature_name: weight}`.

    Per-cell z-scoring (not global) means the weights are scale-free
    and compare features on equal footing — important because OFI's
    raw scale differs from microprice_dev's by ~10⁴.
    """
    feat_names = list(features.keys())
    z_chunks: list[np.ndarray] = []
    ret_chunks: list[np.ndarray] = []
    for cp in canon_paths:
        bbo = _prepare_bbo(bc.read_bbos(cp))
        for iid, grp in bbo.groupby("instrument_id", sort=True):
            grp = grp.reset_index(drop=True)
            ret = _forward_return(grp, horizon_ns).values
            cols = [fn(grp).values for fn in features.values()]
            mat = np.column_stack(cols)
            valid = ~np.isnan(ret) & ~np.any(np.isnan(mat), axis=1)
            if int(valid.sum()) < min_ticks:
                continue
            # Per-cell z-score so each feature contributes on a 0/1 scale.
            zs = np.column_stack([_z(mat[valid, i]) for i in range(mat.shape[1])])
            z_chunks.append(zs)
            ret_chunks.append(ret[valid])
    if not z_chunks:
        return {n: float("nan") for n in feat_names}
    Z = np.vstack(z_chunks)
    Y = np.concatenate(ret_chunks)
    X = np.column_stack([Z, np.ones(len(Y))])
    coef, *_ = np.linalg.lstsq(X, Y, rcond=None)
    return {feat_names[i]: float(coef[i]) for i in range(len(feat_names))}


def composite_signal(
    features: dict[str, FeatureFn],
    weights: dict[str, float],
) -> FeatureFn:
    """Build a feature function that z-scores each component per call
    and combines with `weights`. The returned callable is compatible
    with `ic_panel(features={'composite': composite_signal(...)})`.

    Important: z-scoring happens **inside the cell** (per call) so the
    returned signal is comparable across instruments. This matches how
    `fit_composite_weights` constructed the training data.
    """
    feat_names = list(features.keys())

    def _composite(bbo: pd.DataFrame) -> pd.Series:
        vals = np.zeros(len(bbo), dtype=float)
        for name in feat_names:
            raw = features[name](bbo).values
            vals += weights.get(name, 0.0) * _z(raw)
        return pd.Series(vals, index=bbo.index, name="composite")

    return _composite

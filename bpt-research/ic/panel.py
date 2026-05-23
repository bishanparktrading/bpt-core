"""IC panel — feature-fn agnostic cross-day predictiveness study.

`ic_panel` streams a list of canon files, computes one row per
(day, instrument, feature) with the univariate Spearman IC, OLS slope,
and feature std. `aggregate` rolls up to per-(instrument, feature)
IC mean / std across days, plus a stability flag.

Multivariate partial slopes are intentionally NOT bundled here —
that's a separate concern (different inputs: regressor list ≠ feature
list). Use `ic.multivariate` if you need it (TODO).
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable

import numpy as np
import pandas as pd
from scipy.stats import spearmanr

import bpt_canon as bc

FeatureFn = Callable[[pd.DataFrame], pd.Series]


def _prepare_bbo(bbo: pd.DataFrame) -> pd.DataFrame:
    """Drop crossed/zero rows, add mid column. Returns a new frame."""
    bbo = bbo[(bbo.bid > 0) & (bbo.ask > 0) & (bbo.ask > bbo.bid)].reset_index(drop=True)
    bbo["mid"] = (bbo.bid + bbo.ask) * 0.5
    return bbo


def _forward_return(bbo: pd.DataFrame, horizon_ns: int) -> pd.Series:
    """ret = log(mid_{t+horizon} / mid_t) via merge_asof(direction='forward').

    Strictly future — see findings doc caveat #5. Negative `horizon_ns`
    yields a backward look (useful for lead-lag analysis); merge_asof
    with direction='backward' would handle that.
    """
    target = bbo.ts_ns + horizon_ns
    direction = "forward" if horizon_ns >= 0 else "backward"
    fwd = pd.merge_asof(
        pd.DataFrame({"target_ts": target}),
        bbo[["ts_ns", "mid"]].rename(columns={"ts_ns": "fwd_ts", "mid": "fwd_mid"}),
        left_on="target_ts", right_on="fwd_ts", direction=direction,
    )
    return pd.Series(
        np.log(fwd.fwd_mid.values / bbo.mid.values),
        index=bbo.index,
    )


def ic_panel(
    canon_paths: list[Path],
    features: dict[str, FeatureFn],
    *,
    horizon_ns: int = 1_000_000_000,
    min_ticks: int = 500,
    progress: bool = True,
) -> pd.DataFrame:
    """One row per (day, instrument, feature) with IC + slope + std.

    Args:
        canon_paths:  canon files to read (one day's worth each by convention)
        features:     {name: callable(bbo_df) -> Series indexed like bbo_df}
        horizon_ns:   forward-return horizon, in nanoseconds (default 1s)
        min_ticks:    drop (day, instrument) cells with fewer valid rows
                      to keep low-N noise out of the panel
        progress:     print per-cell line if True

    Returns:
        DataFrame with columns: day, instrument_id, feature, n, ic,
        beta_uni, feature_std.
    """
    rows = []
    for cp in canon_paths:
        day = cp.stem
        bbo = _prepare_bbo(bc.read_bbos(cp))
        for iid, grp in bbo.groupby("instrument_id", sort=True):
            grp = grp.reset_index(drop=True)
            ret = _forward_return(grp, horizon_ns)
            cell_ics: dict[str, float] = {}
            for fname, fn in features.items():
                sig = fn(grp)
                mask = ret.notna() & sig.notna()
                n = int(mask.sum())
                if n < min_ticks:
                    rows.append({
                        "day": day, "instrument_id": int(iid),
                        "feature": fname, "n": n,
                        "ic": np.nan, "beta_uni": np.nan,
                        "feature_std": np.nan,
                    })
                    cell_ics[fname] = float("nan")
                    continue
                ic, _ = spearmanr(sig[mask], ret[mask])
                beta = np.polyfit(sig[mask].values, ret[mask].values, 1)[0]
                rows.append({
                    "day": day, "instrument_id": int(iid),
                    "feature": fname, "n": n,
                    "ic": float(ic),
                    "beta_uni": float(beta),
                    "feature_std": float(sig[mask].std()),
                })
                cell_ics[fname] = float(ic)
            if progress:
                feats_str = "  ".join(f"{f}={v:+.3f}" for f, v in cell_ics.items())
                print(f"  {day} iid={iid:5d}  n={n:6d}  {feats_str}", flush=True)
    return pd.DataFrame(rows)


def aggregate(
    panel: pd.DataFrame,
    *,
    ic_std_threshold: float = 0.08,
) -> pd.DataFrame:
    """Per-(instrument, feature) cross-day rollup.

    Returns one row per (instrument_id, feature) with IC mean / std,
    OLS slope mean, mean feature std, and a stability flag (IC std
    below threshold). Sorted by feature then IC mean.
    """
    g = panel.dropna(subset=["ic"]).groupby(["instrument_id", "feature"])
    out = g.agg(
        days=("day", "nunique"),
        ic_mean=("ic", "mean"),
        ic_std=("ic", "std"),
        beta_mean=("beta_uni", "mean"),
        feature_std_mean=("feature_std", "mean"),
    ).reset_index()
    out["stable"] = out.ic_std < ic_std_threshold
    return out.sort_values(["feature", "ic_mean"], ascending=[True, False]).reset_index(drop=True)

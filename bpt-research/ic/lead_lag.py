"""Lead-lag IC scan — lookahead-bias detector.

A real, causal signal must be approximately uncorrelated with *past*
returns (horizons ≤ 0) and predictive only of *future* returns
(horizons > 0). If IC is non-zero at h ≤ 0, the signal is either
peeking at the future (bug) or accidentally co-moving with something
slower than the test horizon (still informative, but not a *predictor*).

Output is the IC value at each tested horizon, per instrument. Plot
horizon on the x-axis: a real signal looks like a near-zero floor for
h ≤ 0 that rises and peaks for some h > 0, then decays back to zero.
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable

import numpy as np
import pandas as pd
from scipy.stats import spearmanr

import bpt_canon as bc

from .panel import _prepare_bbo, _forward_return

FeatureFn = Callable[[pd.DataFrame], pd.Series]

# Default sweep — log-ish spacing across ±10s, with 0 included.
# Negative = backward (return ending at t), positive = forward.
_DEFAULT_HORIZONS_NS: dict[str, int] = {
    "-10s":   -10_000_000_000,
    "-1s":    -1_000_000_000,
    "-100ms": -100_000_000,
    "0":       0,
    "+100ms":  100_000_000,
    "+500ms":  500_000_000,
    "+1s":     1_000_000_000,
    "+5s":     5_000_000_000,
    "+10s":   10_000_000_000,
}


def lead_lag_ic(
    canon_paths: list[Path],
    feature_fn: FeatureFn,
    *,
    horizons_ns: dict[str, int] | None = None,
    min_ticks: int = 500,
    pool_across_days: bool = True,
) -> pd.DataFrame:
    """IC at multiple horizons per instrument.

    Returns DataFrame with columns: instrument_id, horizon_label,
    horizon_ns, ic_mean (across days), ic_std, days, n_mean.

    With `pool_across_days=False`, returns per-(day, instrument, horizon)
    cells instead of averaging — useful if you want to see day-to-day
    horizon decay variation.
    """
    horizons = horizons_ns or _DEFAULT_HORIZONS_NS
    rows = []
    for cp in canon_paths:
        day = cp.stem
        bbo = _prepare_bbo(bc.read_bbos(cp))
        for iid, grp in bbo.groupby("instrument_id", sort=True):
            grp = grp.reset_index(drop=True)
            sig = feature_fn(grp)
            for label, h_ns in horizons.items():
                if h_ns == 0:
                    # IC of signal vs *current* mid — by definition the
                    # signal can't predict a return that hasn't happened
                    # yet; use the zero-horizon return (= 0) as the
                    # natural floor. Skip Spearman (constant 0 vector).
                    rows.append({
                        "day": day, "instrument_id": int(iid),
                        "horizon_label": label, "horizon_ns": h_ns,
                        "ic": 0.0, "n": int(sig.notna().sum()),
                    })
                    continue
                ret = _forward_return(grp, h_ns)
                mask = ret.notna() & sig.notna()
                n = int(mask.sum())
                if n < min_ticks:
                    rows.append({
                        "day": day, "instrument_id": int(iid),
                        "horizon_label": label, "horizon_ns": h_ns,
                        "ic": np.nan, "n": n,
                    })
                    continue
                ic, _ = spearmanr(sig[mask], ret[mask])
                rows.append({
                    "day": day, "instrument_id": int(iid),
                    "horizon_label": label, "horizon_ns": h_ns,
                    "ic": float(ic), "n": n,
                })

    panel = pd.DataFrame(rows)
    if not pool_across_days:
        return panel

    pooled = (
        panel.dropna(subset=["ic"])
             .groupby(["instrument_id", "horizon_label", "horizon_ns"])
             .agg(days=("day", "nunique"),
                  ic_mean=("ic", "mean"),
                  ic_std=("ic", "std"),
                  n_mean=("n", "mean"))
             .reset_index()
    )
    # Sort by instrument then horizon (timeline order)
    return pooled.sort_values(["instrument_id", "horizon_ns"]).reset_index(drop=True)

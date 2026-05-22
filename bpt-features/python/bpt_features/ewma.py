"""DataFrame wrappers around the EWMA C++ classes.

Same one-row-at-a-time pattern as ofi() and realized_vol(). Per-row
Python overhead is acceptable at canon scale (10s/day-of-data); if a
feature becomes hot enough to matter, the fix is a batch C++ method,
not a vectorised Python rewrite.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from bpt_features._core import EwmaDrift, EwmaVariance

if TYPE_CHECKING:
    import pandas as pd


def ewma_variance(
    df: "pd.DataFrame",
    *,
    halflife_s: float,
    mid_col: str = "mid",
    ts_col: str = "ts_ns",
) -> "pd.Series":
    """Per-second variance σ² EWMA. Returns Series indexed like `df`, name='ewma_var'."""
    import pandas as pd

    est = EwmaVariance(halflife_s)
    out = []
    for mid, ts in zip(df[mid_col], df[ts_col]):
        est.update(float(mid), int(ts))
        out.append(est.value())
    return pd.Series(out, index=df.index, name="ewma_var")


def ewma_drift(
    df: "pd.DataFrame",
    *,
    halflife_s: float,
    mid_col: str = "mid",
    ts_col: str = "ts_ns",
) -> "pd.Series":
    """Per-√second drift µ EWMA. Returns Series indexed like `df`, name='ewma_drift'."""
    import pandas as pd

    est = EwmaDrift(halflife_s)
    out = []
    for mid, ts in zip(df[mid_col], df[ts_col]):
        est.update(float(mid), int(ts))
        out.append(est.value())
    return pd.Series(out, index=df.index, name="ewma_drift")

"""DataFrame wrappers around RegimeDetector and RegimeClassifier.

Both produce two parallel Series — the regime label per row + the
underlying scalar (hurst / trend-zscore) — since notebook research
usually wants both.
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Optional

from bpt_features._core import RegimeClassifier as _RC
from bpt_features._core import RegimeDetector as _RD

if TYPE_CHECKING:
    import pandas as pd


def regime_detector(
    df: "pd.DataFrame",
    *,
    config: Optional[_RD.Config] = None,
    mid_col: str = "mid",
) -> "pd.DataFrame":
    """Hurst-based regime per row.

    Returns DataFrame indexed like `df` with columns: `regime` (str),
    `hurst` (float).
    """
    import pandas as pd

    det = _RD(config) if config is not None else _RD()
    regimes = []
    hursts = []
    for mid in df[mid_col]:
        det.update(float(mid))
        regimes.append(det.regime_name())
        hursts.append(det.hurst())
    return pd.DataFrame({"regime": regimes, "hurst": hursts}, index=df.index)


def regime_classifier(
    df: "pd.DataFrame",
    *,
    config: Optional[_RC.Config] = None,
    mid_col: str = "mid",
    ts_col: str = "ts_ns",
) -> "pd.DataFrame":
    """Vol + trend-z-score regime per row.

    Returns DataFrame indexed like `df` with columns: `regime` (str —
    QUIET/TRENDING/CHOPPY), `rv_bps_per_min` (float), `trend_z` (float).
    """
    import pandas as pd

    clf = _RC(config) if config is not None else _RC()
    regimes = []
    rvs = []
    tzs = []
    for mid, ts in zip(df[mid_col], df[ts_col]):
        clf.update(float(mid), int(ts))
        r = clf.classify(int(ts))
        regimes.append(r.name)
        rvs.append(clf.realized_vol_bps_per_min())
        tzs.append(clf.trend_zscore())
    return pd.DataFrame(
        {"regime": regimes, "rv_bps_per_min": rvs, "trend_z": tzs},
        index=df.index,
    )

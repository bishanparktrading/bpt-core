"""Canonical feature definitions for IC research.

Each function takes a BBO DataFrame with columns
  ts_ns, bid, ask, bid_qty, ask_qty, mid
and returns a pd.Series indexed like the input, named for the feature.

Implementations call into bpt_features' C++ via pybind11 — same code AS
uses on the live tick path, so research and production can't drift.
"""

from __future__ import annotations

import numpy as np
import pandas as pd

import bpt_features as bf


def ofi(
    bbo: pd.DataFrame,
    *,
    window_ns: int = 1_000_000_000,
    max_levels: int = 1,
) -> pd.Series:
    """Streaming OFICalculator output, one value per row."""
    cfg = bf.OFICalculator.Config()
    cfg.max_levels = max_levels
    cfg.window_ns = window_ns
    calc = bf.OFICalculator(cfg)
    out = np.empty(len(bbo), dtype=float)
    for i, r in enumerate(bbo.itertuples(index=False)):
        out[i] = calc.update(
            bids=[(r.bid, r.bid_qty)],
            asks=[(r.ask, r.ask_qty)],
            timestamp_ns=r.ts_ns,
        )
    return pd.Series(out, index=bbo.index, name="ofi")


def microprice_dev(bbo: pd.DataFrame) -> pd.Series:
    """microprice − mid via FairValueEstimator(Mode.Micro).

    Matches AS's production microprice computation rather than re-deriving
    the formula. Positive = book tilted toward ask (buy pressure).
    """
    cfg = bf.FairValueEstimator.Config()
    cfg.mode = bf.FairValueEstimator.Mode.Micro
    fve = bf.FairValueEstimator(cfg)
    micro = np.empty(len(bbo), dtype=float)
    for i, r in enumerate(bbo.itertuples(index=False)):
        micro[i] = fve.estimate(
            bid_px=r.bid, ask_px=r.ask,
            bid_qty=r.bid_qty, ask_qty=r.ask_qty,
        )
    return pd.Series(micro - bbo["mid"].values, index=bbo.index, name="microprice_dev")


def drift(bbo: pd.DataFrame, *, halflife_s: float = 30.0) -> pd.Series:
    """EWMA mid-drift (per √s), AS default halflife."""
    return bf.ewma_drift(bbo, halflife_s=halflife_s).rename("drift")


def sigma2(bbo: pd.DataFrame, *, halflife_s: float = 60.0) -> pd.Series:
    """EWMA per-second mid-variance, AS default halflife."""
    return bf.ewma_variance(bbo, halflife_s=halflife_s).rename("sigma2")

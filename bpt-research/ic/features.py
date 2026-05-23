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


def trade_flow_ewma(
    bbo: pd.DataFrame,
    trades: pd.DataFrame,
    *,
    halflife_s: float = 1.0,
) -> pd.Series:
    """Signed taker-volume EWMA sampled at each BBO timestamp.

    Each trade contributes +qty if buyer-aggressive (side=BUY, 0) or
    −qty if seller-aggressive (side=SELL, 1). The running sum decays
    exponentially in continuous time with the given halflife; the
    feature value at BBO time t is the decayed sum at t.

    Pure-Python implementation — bpt_features doesn't yet expose a
    generic scalar EWMA. If this feature earns its keep, promote to
    a C++ class for live use.

    Distinct from OFI: OFI is *resting* book-side imbalance over a
    window. trade_flow_ewma is *aggressing* side bias from actual
    fills. Same direction (positive = buy pressure) but different
    mechanism, so expected correlation is moderate, not high.
    """
    decay_per_ns = np.log(2.0) / (halflife_s * 1e9)

    # Drop NULL side rows (255). Keep only BUY (0) / SELL (1).
    side = trades["side"].values
    keep = side < 2
    t = trades["ts_ns"].values[keep].astype(np.int64)
    signed = trades["qty"].values[keep] * np.where(side[keep] == 0, 1.0, -1.0)

    if len(t) == 0:
        return pd.Series(np.zeros(len(bbo)), index=bbo.index, name="trade_flow_ewma")

    # State at each trade event: decay-from-previous + add new signed qty.
    state_at_trade = np.empty(len(t), dtype=np.float64)
    state = 0.0
    last_t = t[0]
    for i in range(len(t)):
        if i > 0:
            state *= np.exp(-decay_per_ns * float(t[i] - last_t))
            last_t = t[i]
        state += signed[i]
        state_at_trade[i] = state

    # For each BBO tick, find the most recent trade index via searchsorted,
    # then decay state_at_trade from that timestamp forward to the BBO ts.
    bbo_ts = bbo["ts_ns"].values.astype(np.int64)
    trade_idx = np.searchsorted(t, bbo_ts, side="right") - 1

    out = np.zeros(len(bbo_ts), dtype=np.float64)
    valid = trade_idx >= 0
    if valid.any():
        s = state_at_trade[trade_idx[valid]]
        dt = (bbo_ts[valid] - t[trade_idx[valid]]).astype(np.float64)
        out[valid] = s * np.exp(-decay_per_ns * dt)
    return pd.Series(out, index=bbo.index, name="trade_flow_ewma")


def sigma2(bbo: pd.DataFrame, *, halflife_s: float = 60.0) -> pd.Series:
    """EWMA per-second mid-variance, AS default halflife."""
    return bf.ewma_variance(bbo, halflife_s=halflife_s).rename("sigma2")

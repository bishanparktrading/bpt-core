"""Durable, cache-backed loaders for research panels.

The discovery loop pattern: notebooks should *query* materialized data,
not *compute* it every cell. This module provides cached builders that
turn raw canon files into the derived panels research notebooks actually
want — trades joined with pre-trade features, markouts at standard
horizons, etc.

Cache layout
------------
    ~/bpt-data/
        panels/
            ofi_<hash>.parquet     # per-instrument trade + OFI + markout panel
            ...

Each cached file's hash is derived from its input arguments + the source
code version of the builder function. Change either, and the cache key
changes, forcing a rebuild. Same inputs + same code → instant load.

Usage
-----
    from bpt_research.loaders import load_ofi_panel

    panel = load_ofi_panel(
        canon_paths=sorted(Path('/opt/bpt/data/canon/hl').glob('*.canon')),
        ofi_window_ns=1_000_000_000,
        markout_horizon_ns=1_000_000_000,
    )
    # panel is a DataFrame with columns:
    #   instrument_id, side, pre_trade_ofi, markout_bps
"""

from __future__ import annotations

import hashlib
import inspect
import sys
from pathlib import Path
from typing import Iterable

import numpy as np
import pandas as pd

DATA_ROOT = Path.home() / "bpt-data"
PANELS_DIR = DATA_ROOT / "panels"

# Ensure the bpt_canon + bpt_features imports work from a notebook
# without per-notebook sys.path twiddling.
_REPO = Path(__file__).resolve().parents[1]
for _p in (
    _REPO / "bpt-canon" / "python",
    _REPO / "bpt-features" / "python",
    _REPO / "bazel-bin" / "bpt-features" / "python",
):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))


def _source_hash(fn) -> str:
    """Hash the source of `fn` so cache invalidates when the builder changes."""
    src = inspect.getsource(fn)
    return hashlib.sha256(src.encode()).hexdigest()[:8]


def _args_hash(*args, **kwargs) -> str:
    """Stable short hash of call arguments. Paths are stringified."""
    norm_args = tuple(
        sorted(str(p) for p in a) if isinstance(a, (list, tuple, set)) else str(a)
        for a in args
    )
    norm_kwargs = tuple(sorted((k, str(v)) for k, v in kwargs.items()))
    blob = repr((norm_args, norm_kwargs))
    return hashlib.sha256(blob.encode()).hexdigest()[:16]


def _cache_path(name: str, fn, *args, **kwargs) -> Path:
    """Build the canonical cache filename for a memoized panel."""
    key = f"{_source_hash(fn)}_{_args_hash(*args, **kwargs)}"
    return PANELS_DIR / f"{name}_{key}.parquet"


# ── OFI + markout panel ──────────────────────────────────────────────────


def _build_ofi_panel(canon_paths: list[Path], ofi_window_ns: int, markout_horizon_ns: int,
                    min_trades_per_instrument: int = 200) -> pd.DataFrame:
    """Compute the (trade, pre-trade OFI, markout) panel from canon files.

    Same shape as bpt-research/experiments/ofi_cancel_spread_aware.py's
    `collect_for_instrument`. Pools across all canon paths + instruments
    into one DataFrame keyed by instrument_id.
    """
    import bpt_canon as bc
    from ic import ofi as ic_ofi
    from ic.panel import _prepare_bbo

    rows: list[pd.DataFrame] = []
    for cp in canon_paths:
        bbo = _prepare_bbo(bc.read_bbos(cp))
        trades = bc.read_trades(cp)
        for iid, grp in bbo.groupby("instrument_id", sort=True):
            grp = grp.reset_index(drop=True)
            tr_i = trades[trades.instrument_id == iid].reset_index(drop=True)
            if len(tr_i) < min_trades_per_instrument:
                continue
            ofi_at_bbo = ic_ofi(grp).values
            bbo_ts = grp["ts_ns"].values
            bbo_mid = grp["mid"].values

            tr_i = tr_i[tr_i["side"] < 2].reset_index(drop=True)
            if len(tr_i) == 0:
                continue
            trade_ts = tr_i["ts_ns"].values
            bbo_idx = np.searchsorted(bbo_ts, trade_ts, side="right") - 1
            valid = bbo_idx >= 0
            pre_trade_ofi = np.where(valid, ofi_at_bbo[bbo_idx], np.nan)
            anchor_mid = np.where(valid, bbo_mid[bbo_idx], np.nan)

            future_ts = trade_ts + markout_horizon_ns
            fut_idx = np.searchsorted(bbo_ts, future_ts, side="left")
            fut_valid = fut_idx < len(bbo_ts)
            fut_mid = np.where(fut_valid, bbo_mid[np.minimum(fut_idx, len(bbo_ts) - 1)], np.nan)
            markout_bps = np.log(fut_mid / anchor_mid) * 1e4

            df = pd.DataFrame(
                {
                    "side": tr_i["side"].values,
                    "pre_trade_ofi": pre_trade_ofi,
                    "markout_bps": markout_bps,
                }
            ).dropna(subset=["pre_trade_ofi", "markout_bps"])
            df["instrument_id"] = int(iid)
            rows.append(df)

    if not rows:
        return pd.DataFrame(
            columns=["instrument_id", "side", "pre_trade_ofi", "markout_bps"]
        )
    return pd.concat(rows, ignore_index=True)


def load_ofi_panel(
    canon_paths: Iterable[Path],
    *,
    ofi_window_ns: int = 1_000_000_000,
    markout_horizon_ns: int = 1_000_000_000,
    min_trades_per_instrument: int = 200,
    rebuild: bool = False,
) -> pd.DataFrame:
    """Load the (trade, pre-trade OFI, markout) panel, cached to parquet.

    First call with a given (canon_paths, params, code) tuple computes and
    writes to `~/bpt-data/panels/ofi_<hash>.parquet`. Subsequent calls are
    a single parquet read.

    Parameters
    ----------
    canon_paths : iterable of Path
        Canon files to pool. Order-independent (sorted internally).
    ofi_window_ns : int
        Reserved for future variants; today the OFI is computed by
        `bpt_research.ic.ofi` which has its own window config.
    markout_horizon_ns : int
        Forward-return horizon for the markout column. Default 1s.
    min_trades_per_instrument : int
        Instruments below this trade count are dropped from the panel.
    rebuild : bool
        Force recomputation even if the cache file exists.

    Returns
    -------
    DataFrame with columns: instrument_id, side, pre_trade_ofi, markout_bps.
    """
    paths = sorted(Path(p) for p in canon_paths)
    cache = _cache_path(
        "ofi",
        _build_ofi_panel,
        paths,
        ofi_window_ns,
        markout_horizon_ns,
        min_trades_per_instrument,
    )
    if cache.exists() and not rebuild:
        return pd.read_parquet(cache)

    panel = _build_ofi_panel(
        paths,
        ofi_window_ns=ofi_window_ns,
        markout_horizon_ns=markout_horizon_ns,
        min_trades_per_instrument=min_trades_per_instrument,
    )
    cache.parent.mkdir(parents=True, exist_ok=True)
    panel.to_parquet(cache, index=False)
    return panel


def cache_info() -> pd.DataFrame:
    """Inventory of cached panel files. Useful for debugging cache hits/misses."""
    if not PANELS_DIR.exists():
        return pd.DataFrame(columns=["name", "size_mb", "mtime"])
    rows = []
    for p in sorted(PANELS_DIR.glob("*.parquet")):
        rows.append(
            {
                "name": p.name,
                "size_mb": p.stat().st_size / 1e6,
                "mtime": pd.Timestamp.fromtimestamp(p.stat().st_mtime),
            }
        )
    return pd.DataFrame(rows)


def clear_cache() -> int:
    """Wipe all cached panels. Returns number of files removed."""
    if not PANELS_DIR.exists():
        return 0
    n = 0
    for p in PANELS_DIR.glob("*.parquet"):
        p.unlink()
        n += 1
    return n

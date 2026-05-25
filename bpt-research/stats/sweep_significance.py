"""Apply PSR + DSR to existing sweep results.

Reads per-cell trades.csv from a sweep root, pools per-fill realized_pnl
across days within each (sweep, theta) bucket, computes Sharpe + PSR +
DSR. Output: a table of (sweep, theta) → (n_fills, SR, PSR, DSR).

Usage:
    python3 sweep_significance.py --results-dir bpt-backtester/results
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from stats.significance import (  # noqa: E402
    deflated_sharpe_ratio,
    probabilistic_sharpe_ratio,
    sharpe_ratio,
)


# Sweep results are staged as `<sweep_name>__run_<theta>_<date>[_<hr>]/`.
# Extract sweep + theta to bucket cells. The optional `_<hr>` suffix is
# used by all-hours sweeps; older 1-hour-per-day sweeps stop at the date.
_RUN_RE = re.compile(r"^(.+)__run_([^_]+)_(\d{4}-\d{2}-\d{2})(?:_\d+)?$")


def load_pnl_by_bucket(results_dir: Path) -> dict[tuple[str, str], np.ndarray]:
    """Return {(sweep, theta): array of per-fill realized_pnl pooled across days}."""
    buckets: dict[tuple[str, str], list[float]] = defaultdict(list)
    for run_dir in sorted(results_dir.iterdir()):
        if not run_dir.is_dir():
            continue
        m = _RUN_RE.match(run_dir.name)
        if not m:
            continue
        sweep, theta, _date = m.group(1), m.group(2), m.group(3)
        trades_path = run_dir / "trades.csv"
        if not trades_path.exists():
            continue
        df = pd.read_csv(trades_path)
        if "realized_pnl" not in df.columns or df.empty:
            continue
        pnls = df["realized_pnl"].astype(float).tolist()
        buckets[(sweep, theta)].extend(pnls)
    return {k: np.asarray(v, dtype=float) for k, v in buckets.items()}


def compute_table(buckets: dict[tuple[str, str], np.ndarray]) -> pd.DataFrame:
    """One row per (sweep, theta) with SR, PSR, DSR + N."""
    # Count thetas per sweep — that's the multi-testing num_trials for DSR.
    trials_per_sweep: dict[str, int] = defaultdict(int)
    for (sweep, _theta) in buckets:
        trials_per_sweep[sweep] += 1

    # Per-sweep SR variance for DSR's expected-max term. Use the
    # cross-cell SR dispersion within the sweep as the noise floor.
    sr_by_sweep: dict[str, list[float]] = defaultdict(list)
    for (sweep, theta), pnls in buckets.items():
        sr_by_sweep[sweep].append(sharpe_ratio(pnls))

    rows = []
    for (sweep, theta), pnls in sorted(buckets.items()):
        n = len(pnls)
        if n == 0:
            sr = 0.0
            skew = kurt = np.nan
            psr = dsr = np.nan
        else:
            sr = sharpe_ratio(pnls)
            mu = float(np.mean(pnls))
            sigma = float(np.std(pnls, ddof=1)) if n > 1 else 0.0
            if sigma > 0 and n > 2:
                z = (pnls - mu) / sigma
                skew = float(np.mean(z ** 3))
                kurt = float(np.mean(z ** 4))
            else:
                skew, kurt = 0.0, 3.0
            psr = probabilistic_sharpe_ratio(sr, n=n, skew=skew, kurt=kurt)
            sr_var = float(np.var(sr_by_sweep[sweep], ddof=1)) if len(sr_by_sweep[sweep]) > 1 else 1.0
            dsr = deflated_sharpe_ratio(
                sr_hat=sr,
                n=n,
                num_trials=trials_per_sweep[sweep],
                sr_variance=max(sr_var, 1e-9),
                skew=skew,
                kurt=kurt,
            )
        rows.append(
            {
                "sweep": sweep,
                "theta": theta,
                "n_fills": n,
                "total_pnl": float(np.sum(pnls)) if n else 0.0,
                "sharpe": sr,
                "skew": skew,
                "kurt": kurt,
                "psr": psr,
                "dsr": dsr,
            }
        )
    return pd.DataFrame(rows)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--results-dir",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "bpt-backtester" / "results",
        help="Sweep results parent directory (default: bpt-backtester/results)",
    )
    args = p.parse_args(argv)

    buckets = load_pnl_by_bucket(args.results_dir)
    if not buckets:
        print(f"No buckets found under {args.results_dir}")
        return 1

    table = compute_table(buckets)
    pd.set_option("display.float_format", lambda v: f"{v:+.4f}" if not pd.isna(v) else "—")
    pd.set_option("display.max_rows", 100)
    print(table.to_string(index=False))

    print("\nInterpretation guide:")
    print("  PSR > 0.95 = strong confidence true SR > 0")
    print("  DSR > 0.95 = strong confidence true SR > expected-max-under-noise")
    print("  Both < 0.95 = the observed SR could plausibly be sampling noise")
    return 0


if __name__ == "__main__":
    sys.exit(main())

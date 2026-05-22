"""Append a row to experiments.db for every completed backtest run.

Reads `<run_dir>/summary.json` and inserts the metrics — backtester
already writes everything we need (see
bpt-backtester/src/results/results_collector.cpp). Idempotent on
re-runs of the same run_id via INSERT OR REPLACE.

Notebook query side stays in DuckDB / pandas: the .db file is plain
SQLite, every reader speaks it.
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path
from typing import Optional

# Columns we lift out of summary.json into typed SQL columns.
# Everything else is preserved in the full_json blob.
_NUMERIC_FIELDS = [
    "starting_capital",
    "final_equity",
    "total_pnl",
    "return_pct",
    "max_drawdown_pct",
    "sharpe_per_fill",
    "wallclock_duration_ms",
    "total_fills",
    "win_fills",
    "win_rate_pct",
    "buy_count",
    "sell_count",
    "buy_notional_usd",
    "sell_notional_usd",
    "fees_paid_usd",
    "maker_fills",
    "taker_fills",
]

_TEXT_FIELDS = [
    "strategy_name",
    "params_hash",
    "git_sha",
    "simulation_start",
    "simulation_end",
]

_SCHEMA = """
CREATE TABLE IF NOT EXISTS experiments (
    run_id                TEXT PRIMARY KEY,
    strategy_name         TEXT,
    params_hash           TEXT,
    git_sha               TEXT,
    simulation_start      TEXT,
    simulation_end        TEXT,
    wallclock_duration_ms INTEGER,
    starting_capital      REAL,
    final_equity          REAL,
    total_pnl             REAL,
    return_pct            REAL,
    max_drawdown_pct      REAL,
    sharpe_per_fill       REAL,
    total_fills           INTEGER,
    win_fills             INTEGER,
    win_rate_pct          REAL,
    buy_count             INTEGER,
    sell_count            INTEGER,
    buy_notional_usd      REAL,
    sell_notional_usd     REAL,
    fees_paid_usd         REAL,
    maker_fills           INTEGER,
    taker_fills           INTEGER,
    instruments_json      TEXT,
    full_json             TEXT,
    inserted_at_ts        TEXT DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_strategy    ON experiments(strategy_name);
CREATE INDEX IF NOT EXISTS idx_params_hash ON experiments(params_hash);
CREATE INDEX IF NOT EXISTS idx_sharpe      ON experiments(sharpe_per_fill);
CREATE INDEX IF NOT EXISTS idx_pnl         ON experiments(total_pnl);
"""


def init_db(db_path: Path) -> None:
    """Create schema if not present. Safe to call repeatedly."""
    db_path.parent.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(db_path) as conn:
        conn.executescript(_SCHEMA)


def ingest_run_dir(db_path: Path, run_dir: Path, run_id: Optional[str] = None) -> str:
    """Read run_dir/summary.json and INSERT OR REPLACE one row.

    run_id defaults to the directory name (the convention sweep.py uses).
    Returns the run_id inserted.
    """
    summary_path = run_dir / "summary.json"
    if not summary_path.exists():
        raise FileNotFoundError(f"no summary.json in {run_dir}")

    with open(summary_path) as f:
        summary = json.load(f)

    rid = run_id or run_dir.name
    row = {"run_id": rid}
    for field in _TEXT_FIELDS:
        row[field] = summary.get(field)
    for field in _NUMERIC_FIELDS:
        v = summary.get(field)
        # SQLite is fine with None/int/float; reject anything else loudly.
        if v is not None and not isinstance(v, (int, float)):
            raise TypeError(f"{field} is {type(v).__name__}, expected number/None")
        row[field] = v

    instruments = summary.get("instruments")
    row["instruments_json"] = json.dumps(instruments) if instruments is not None else None
    row["full_json"] = json.dumps(summary)

    init_db(db_path)
    cols = list(row.keys())
    placeholders = ",".join(["?"] * len(cols))
    sql = f"INSERT OR REPLACE INTO experiments ({','.join(cols)}) VALUES ({placeholders})"
    with sqlite3.connect(db_path) as conn:
        conn.execute(sql, [row[c] for c in cols])
        conn.commit()
    return rid


def main(argv: Optional[list[str]] = None) -> int:
    p = argparse.ArgumentParser(description="Ingest a backtest results dir into experiments.db")
    p.add_argument("--db", type=Path, default=Path("bpt-research/experiments.db"))
    p.add_argument("--results-dir", type=Path, required=True,
                   help="Directory containing summary.json")
    p.add_argument("--run-id", type=str, default=None,
                   help="Override run_id (default: results dir basename)")
    args = p.parse_args(argv)

    rid = ingest_run_dir(args.db, args.results_dir, args.run_id)
    print(f"ingested run_id={rid} into {args.db}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

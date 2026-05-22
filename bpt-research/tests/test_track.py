"""Round-trip test: fabricate a summary.json, ingest, query back."""

import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "bpt-research"))

from experiments.track import ingest_run_dir, init_db


def _fake_summary() -> dict:
    return {
        "starting_capital": 10_000.0,
        "final_equity": 10_345.67,
        "total_pnl": 345.67,
        "return_pct": 3.4567,
        "max_drawdown_pct": -1.23,
        "sharpe_per_fill": 1.42,
        "total_fills": 137,
        "win_fills": 82,
        "win_rate_pct": 59.85,
        "buy_count": 68,
        "sell_count": 69,
        "buy_notional_usd": 540123.4,
        "sell_notional_usd": 539876.2,
        "fees_paid_usd": 12.34,
        "maker_fills": 130,
        "taker_fills": 7,
        "simulation_start": "2026-05-20T00:00:00",
        "simulation_end": "2026-05-20T16:00:00",
        "wallclock_duration_ms": 12_345,
        "strategy_name": "AvellanedaStoikov",
        "params_hash": "deadbeefcafe0001",
        "git_sha": "abc1234567890",
        "instruments": [{"id": 1, "symbol": "BTC-USDT-SWAP", "exchange": "OKX"}],
        "fees_by_venue": {"OKX": 12.34},
    }


class TestTrack(unittest.TestCase):
    def test_init_db_creates_table(self):
        with tempfile.TemporaryDirectory() as d:
            db = Path(d) / "x.db"
            init_db(db)
            with sqlite3.connect(db) as c:
                cols = [r[1] for r in c.execute("PRAGMA table_info(experiments)")]
            self.assertIn("run_id", cols)
            self.assertIn("sharpe_per_fill", cols)
            self.assertIn("full_json", cols)

    def test_ingest_one_run(self):
        with tempfile.TemporaryDirectory() as d:
            tmp = Path(d)
            run_dir = tmp / "20260520T1600_deadbeef_abc"
            run_dir.mkdir()
            (run_dir / "summary.json").write_text(json.dumps(_fake_summary()))
            db = tmp / "experiments.db"

            rid = ingest_run_dir(db, run_dir)
            self.assertEqual(rid, "20260520T1600_deadbeef_abc")

            with sqlite3.connect(db) as c:
                row = c.execute(
                    "SELECT strategy_name, total_pnl, sharpe_per_fill, win_rate_pct "
                    "FROM experiments WHERE run_id = ?",
                    (rid,),
                ).fetchone()
            self.assertEqual(row[0], "AvellanedaStoikov")
            self.assertAlmostEqual(row[1], 345.67)
            self.assertAlmostEqual(row[2], 1.42)
            self.assertAlmostEqual(row[3], 59.85)

    def test_reingest_is_replace_not_dup(self):
        with tempfile.TemporaryDirectory() as d:
            tmp = Path(d)
            run_dir = tmp / "run1"
            run_dir.mkdir()
            s = _fake_summary()
            (run_dir / "summary.json").write_text(json.dumps(s))
            db = tmp / "experiments.db"

            ingest_run_dir(db, run_dir)
            # Mutate metric, re-ingest under the same dir name.
            s["total_pnl"] = 999.99
            (run_dir / "summary.json").write_text(json.dumps(s))
            ingest_run_dir(db, run_dir)

            with sqlite3.connect(db) as c:
                rows = c.execute("SELECT total_pnl FROM experiments WHERE run_id='run1'").fetchall()
            self.assertEqual(len(rows), 1)
            self.assertAlmostEqual(rows[0][0], 999.99)

    def test_missing_summary_raises(self):
        with tempfile.TemporaryDirectory() as d:
            tmp = Path(d)
            run_dir = tmp / "empty"
            run_dir.mkdir()
            with self.assertRaises(FileNotFoundError):
                ingest_run_dir(tmp / "x.db", run_dir)

    def test_full_json_preserved(self):
        """Notebook can dig into less-common metrics via the JSON blob."""
        with tempfile.TemporaryDirectory() as d:
            tmp = Path(d)
            run_dir = tmp / "run1"
            run_dir.mkdir()
            (run_dir / "summary.json").write_text(json.dumps(_fake_summary()))
            db = tmp / "experiments.db"
            ingest_run_dir(db, run_dir)

            with sqlite3.connect(db) as c:
                (blob,) = c.execute(
                    "SELECT full_json FROM experiments WHERE run_id='run1'"
                ).fetchone()
            recovered = json.loads(blob)
            self.assertEqual(recovered["fees_by_venue"], {"OKX": 12.34})


if __name__ == "__main__":
    unittest.main()

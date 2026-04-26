#!/usr/bin/env python3
"""
Backtest parameter sweep — generates temp strategy configs across the
Cartesian product of supplied param values, runs each through
scripts/backtest.sh, then summarises results into a single table.

Each iteration is fully independent: a unique strategy config file → a
unique params_hash → a unique on-disk run_id under bpt-backtester/results/.
You can re-run the same sweep, only the modified params actually re-run
because the existing run_id directory is left in place (the backtester
writes into it freshly each time, but the dir name comes from identity).

Aeron is single-tenant on /dev/shm/aeron-bpt, so iterations run
strictly serially. For parallel sweeps we'd need per-iteration
media-driver dirs + port windows; deferred until needed.

Usage:
  scripts/sweep.py \
      --base bpt-strategy/config/avellaneda_stoikov.backtest.toml \
      --param fenrir.strategy.params.gamma=0.01,0.05,0.1 \
      --param fenrir.strategy.params.kappa=10,20

  # 3 × 2 = 6 runs.
"""

from __future__ import annotations

import argparse
import copy
import itertools
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import tomllib
from typing import Any

import tomli_w


REPO = pathlib.Path(__file__).resolve().parent.parent
RESULTS_ROOT = REPO / "bpt-backtester" / "results"
BACKTEST_SH = REPO / "scripts" / "backtest.sh"


def parse_param(spec: str) -> tuple[list[str], list[Any]]:
    """`fenrir.strategy.params.gamma=0.01,0.05` → (['fenrir', ...], [0.01, 0.05])"""
    if "=" not in spec:
        raise SystemExit(f"--param expects KEY=v1,v2,...; got {spec!r}")
    key, raw_values = spec.split("=", 1)
    path = key.split(".")
    values: list[Any] = []
    for v in raw_values.split(","):
        v = v.strip()
        # Best-effort coercion: int → float → str. Keeps quoted strings intact.
        try:
            values.append(int(v))
            continue
        except ValueError:
            pass
        try:
            values.append(float(v))
            continue
        except ValueError:
            pass
        values.append(v)
    if not values:
        raise SystemExit(f"--param {key} has no values")
    return path, values


def deep_set(obj: dict, path: list[str], value: Any) -> None:
    cur = obj
    for k in path[:-1]:
        if k not in cur or not isinstance(cur[k], dict):
            raise SystemExit(f"Path {'.'.join(path)} not found in base config")
        cur = cur[k]
    if path[-1] not in cur:
        raise SystemExit(f"Leaf {'.'.join(path)} not found in base config")
    cur[path[-1]] = value


def short_label(path: list[str], value: Any) -> str:
    leaf = path[-1]
    return f"{leaf}={value}"


def run_one(strategy_config: pathlib.Path, refdata_config: pathlib.Path | None) -> bool:
    env = os.environ.copy()
    if refdata_config:
        env["BPT_REFDATA_CONFIG"] = str(refdata_config)
    print(f"  → running backtest with {strategy_config.name}", flush=True)
    p = subprocess.run(
        ["bash", str(BACKTEST_SH), "start", str(strategy_config)],
        env=env,
        cwd=REPO,
        capture_output=True,
        text=True,
    )
    if p.returncode != 0:
        print(f"    start.sh exited {p.returncode}", flush=True)
        print(p.stdout[-500:], flush=True)
        print(p.stderr[-500:], flush=True)
        return False

    # backtest.sh starts the stack and returns; the run is async. Poll the
    # backtester log for completion. The script prints a path; we hardcode
    # the canonical one — easier than parsing.
    log = REPO / "bpt-backtester" / "logs" / "bpt-backtester.log"
    deadline = time.time() + 600
    last_size = 0
    while time.time() < deadline:
        try:
            data = log.read_text(errors="ignore")
        except FileNotFoundError:
            data = ""
        if "Backtest complete" in data and len(data) > last_size:
            break
        last_size = len(data)
        time.sleep(2)

    subprocess.run(["bash", str(BACKTEST_SH), "stop"], cwd=REPO, capture_output=True)
    return "Backtest complete" in data


def find_run_dir_by_params_hash(params_hash_full: str) -> pathlib.Path | None:
    """Look up the produced result dir by full sha256 — naming uses first 8."""
    short = params_hash_full[:8]
    candidates = list(RESULTS_ROOT.glob(f"*_{short}_*"))
    if not candidates:
        return None
    # Pick the most recently modified — sweep runs land in dirs named with
    # the same params_hash so re-runs overwrite the previous summary.
    return max(candidates, key=lambda p: p.stat().st_mtime)


def load_summary(run_dir: pathlib.Path) -> dict | None:
    p = run_dir / "summary.json"
    if not p.exists():
        return None
    try:
        return json.loads(p.read_text())
    except json.JSONDecodeError:
        return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--base", required=True, help="Base strategy TOML to clone for each iteration")
    ap.add_argument("--param", action="append", default=[],
                    help="KEY=v1,v2,... — repeatable for multi-param sweeps (Cartesian product)")
    ap.add_argument("--refdata-config",
                    default=str(REPO / "bpt-refdata" / "config" / "bpt-refdata.backtest-hl.toml"),
                    help="Refdata config to use (forwarded as BPT_REFDATA_CONFIG)")
    ap.add_argument("--dry-run", action="store_true",
                    help="Generate the temp configs but don't run anything")
    args = ap.parse_args()

    base_path = pathlib.Path(args.base).resolve()
    if not base_path.exists():
        raise SystemExit(f"Base config not found: {base_path}")
    refdata_path = pathlib.Path(args.refdata_config).resolve() if args.refdata_config else None

    # backtest.sh accepts the *instance* config; the actual strategy params
    # live in a separate file referenced via `strategy_config = ...`.
    # Resolve that pointer up front so we can sweep the leaf file.
    instance = tomllib.loads(base_path.read_text())
    params_rel = instance.get("strategy_config")
    if not params_rel:
        raise SystemExit(f"{base_path} has no `strategy_config = ...` pointer")
    params_path = (base_path.parent / params_rel).resolve()
    if not params_path.exists():
        raise SystemExit(f"Params file not found: {params_path}")
    base = tomllib.loads(params_path.read_text())

    if not args.param:
        raise SystemExit("Need at least one --param KEY=v1,v2,...")

    parsed = [parse_param(p) for p in args.param]
    paths = [p for p, _ in parsed]
    value_lists = [vs for _, vs in parsed]

    combos = list(itertools.product(*value_lists))
    print(f"Sweep: {len(combos)} run(s) over {[','.join(p) for p in paths]}", flush=True)
    print(f"Base config: {base_path}", flush=True)
    print(f"Refdata config: {refdata_path}", flush=True)
    print(flush=True)

    rows: list[dict] = []
    import hashlib

    for i, combo in enumerate(combos, 1):
        params_cfg = copy.deepcopy(base)
        label_parts = []
        for path, value in zip(paths, combo):
            deep_set(params_cfg, path, value)
            label_parts.append(short_label(path, value))
        label = " ".join(label_parts)

        # Generate a temp params file alongside the original (so any
        # relative paths inside it still resolve), and a temp instance
        # config that points at it. Both go under the same directories
        # they came from so any sibling files keep working.
        params_tmp_name = f"{params_path.stem.split('.')[0]}.sweep-{i:02d}.toml"
        params_tmp_path = params_path.parent / params_tmp_name
        params_tmp_path.write_bytes(tomli_w.dumps(params_cfg).encode())

        instance_cfg = copy.deepcopy(instance)
        instance_cfg["strategy_config"] = (
            os.path.relpath(params_tmp_path, base_path.parent)
        )
        instance_stem = base_path.stem.split(".")[0]
        instance_tmp_name = f"{instance_stem}.sweep-{i:02d}.toml"
        instance_tmp_path = base_path.parent / instance_tmp_name
        instance_tmp_path.write_bytes(tomli_w.dumps(instance_cfg).encode())

        print(f"[{i}/{len(combos)}] {label}", flush=True)
        if args.dry_run:
            print(f"    instance: {instance_tmp_path}")
            print(f"    params:   {params_tmp_path}")
            params_tmp_path.unlink(missing_ok=True)
            instance_tmp_path.unlink(missing_ok=True)
            continue

        try:
            ok = run_one(instance_tmp_path, refdata_path)
            if not ok:
                print(f"    backtest failed — see logs", flush=True)
                continue

            # backtest.sh hashes the file passed to it (the instance config).
            # Mirror that here so we can find the result dir.
            params_hash_full = hashlib.sha256(instance_tmp_path.read_bytes()).hexdigest()
            run_dir = find_run_dir_by_params_hash(params_hash_full)
            if run_dir:
                summary = load_summary(run_dir) or {}
                rows.append({
                    "label": label,
                    "run_id": run_dir.name,
                    "fills": summary.get("total_fills"),
                    "return_pct": summary.get("return_pct"),
                    "max_dd_pct": summary.get("max_drawdown_pct"),
                    "sharpe": summary.get("sharpe_per_fill"),
                    "win_rate": summary.get("win_rate_pct"),
                    "buy_count": summary.get("buy_count"),
                    "sell_count": summary.get("sell_count"),
                })
            else:
                print(f"    no result dir for params_hash={params_hash_full[:8]}", flush=True)
        finally:
            params_tmp_path.unlink(missing_ok=True)
            instance_tmp_path.unlink(missing_ok=True)

    if not rows:
        print("\nNo successful runs.")
        return 1

    print()
    print(f"{'#':>3}  {'params':<40}  {'fills':>6}  {'return%':>9}  {'maxDD%':>8}  {'sharpe':>7}  {'win%':>6}  {'B/S':>7}")
    for i, r in enumerate(rows, 1):
        bs = f"{r['buy_count']}/{r['sell_count']}" if r["buy_count"] is not None else "—"
        ret = f"{(r['return_pct'] or 0) * 1.0:+.4f}" if r["return_pct"] is not None else "—"
        dd = f"{r['max_dd_pct'] or 0:.4f}" if r["max_dd_pct"] is not None else "—"
        sh = f"{r['sharpe'] or 0:+.4f}" if r["sharpe"] is not None else "—"
        wr = f"{r['win_rate'] or 0:.2f}" if r["win_rate"] is not None else "—"
        print(f"{i:>3}  {r['label']:<40}  {r['fills'] or 0:>6}  {ret:>9}  {dd:>8}  {sh:>7}  {wr:>6}  {bs:>7}")

    return 0


if __name__ == "__main__":
    sys.exit(main())

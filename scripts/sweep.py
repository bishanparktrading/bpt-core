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

# Local sweep_lib package.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from sweep_lib.cell_env import CellEnvironment, allocate_cells  # noqa: E402
from sweep_lib.walk_forward import Split, Window, make_splits  # noqa: E402


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

    try:
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

        # backtest.sh starts the stack and returns; the run is async. Poll
        # the backtester log for completion. The script prints a path; we
        # hardcode the canonical one — easier than parsing.
        log = REPO / "bpt-backtester" / "logs" / "bpt-backtester.log"
        deadline = time.time() + 600
        last_size = 0
        data = ""
        while time.time() < deadline:
            try:
                data = log.read_text(errors="ignore")
            except FileNotFoundError:
                data = ""
            if "Backtest complete" in data and len(data) > last_size:
                break
            last_size = len(data)
            time.sleep(2)
        return "Backtest complete" in data
    finally:
        # Always tear down — both on success and on any failure path.
        # A leftover transport / strategy / gateway from a crashed cell
        # would block the next cell's start.sh with "already running".
        # Brief sleep gives kernel time to release bound ports before
        # the next cell tries to bind them.
        subprocess.run(["bash", str(BACKTEST_SH), "stop"], cwd=REPO, capture_output=True)
        # Aeron's shm must be cleared too — backtest.sh stop only kills processes.
        subprocess.run(["rm", "-rf", "/dev/shm/aeron-bpt"], capture_output=True)
        time.sleep(2)


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


def parse_walk_forward(spec: str) -> dict:
    """`start=...,end=...,train=3d,test=1d,step=1d,purge=5m` → dict.

    Day suffix (d) and minute suffix (m) are stripped; ints are returned for
    train_days, test_days, step_days, purge_minutes.
    """
    out: dict[str, Any] = {}
    for kv in spec.split(","):
        if "=" not in kv:
            raise SystemExit(f"--walk-forward expects k=v pairs; got {kv!r}")
        k, v = kv.split("=", 1)
        k = k.strip()
        v = v.strip()
        if k in ("start", "end"):
            out[k] = v
        elif k in ("train", "test", "step"):
            if not v.endswith("d"):
                raise SystemExit(f"--walk-forward {k} requires a day suffix, e.g. {k}=3d")
            out[f"{k}_days"] = int(v[:-1])
        elif k == "purge":
            if not v.endswith("m"):
                raise SystemExit(f"--walk-forward purge requires a minute suffix, e.g. purge=5m")
            out["purge_minutes"] = int(v[:-1])
        else:
            raise SystemExit(f"--walk-forward: unknown key {k!r}")
    for required in ("start", "end", "train_days", "test_days"):
        if required not in out:
            raise SystemExit(f"--walk-forward missing {required.replace('_days', '')}")
    out.setdefault("step_days", 1)
    out.setdefault("purge_minutes", 0)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--base", required=True, help="Base strategy TOML to clone for each iteration")
    ap.add_argument(
        "--param",
        action="append",
        default=[],
        help="KEY=v1,v2,... — repeatable for multi-param sweeps (Cartesian product)",
    )
    ap.add_argument(
        "--walk-forward",
        default=None,
        help="start=...,end=...,train=Nd,test=Nd,step=Nd,purge=Nm — "
        "each split fans out into a TRAIN run + a TEST run; the aggregator "
        "reports both side by side. step and purge are optional.",
    )
    ap.add_argument(
        "--parallel",
        type=int,
        default=1,
        help="Run up to N cells concurrently. Each cell gets a unique "
        "/dev/shm/aeron-bpt-N + isolated metrics/endpoint ports. Default 1 "
        "preserves legacy single-cell behaviour.",
    )
    ap.add_argument(
        "--refdata-config",
        default=str(REPO / "bpt-refdata" / "config" / "bpt-refdata.backtest-hl.toml"),
        help="Refdata config to use (forwarded as BPT_REFDATA_CONFIG)",
    )
    ap.add_argument(
        "--dry-run", action="store_true", help="Generate the temp configs but don't run anything"
    )
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

    if not args.param and not args.walk_forward:
        raise SystemExit("Need at least one --param or --walk-forward")

    parsed = [parse_param(p) for p in args.param]
    paths = [p for p, _ in parsed]
    value_lists = [vs for _, vs in parsed]
    combos = list(itertools.product(*value_lists)) or [()]  # one empty combo if no params

    splits = []
    if args.walk_forward:
        wf = parse_walk_forward(args.walk_forward)
        splits = make_splits(**wf)
        if not splits:
            raise SystemExit(f"--walk-forward produced 0 splits — range too short")

    # Each cell = one backtest run. With walk-forward, every (combo, split)
    # produces TWO cells (train + test). Without, every combo produces one.
    cells: list[tuple[tuple, Split | None, str]] = []
    if splits:
        for combo in combos:
            for split in splits:
                cells.append((combo, split, "train"))
                cells.append((combo, split, "test"))
    else:
        for combo in combos:
            cells.append((combo, None, ""))

    n_cells = len(cells)
    print(
        f"Sweep: {n_cells} cell(s) "
        f"({len(combos)} param combo(s) × {max(len(splits), 1)} split(s) × "
        f"{2 if splits else 1} phase(s))",
        flush=True,
    )
    if paths:
        print(f"  params: {[','.join(p) for p in paths]}", flush=True)
    if splits:
        print(
            f"  splits: {len(splits)} train/test pairs from "
            f"{splits[0].train.start[:10]} → {splits[-1].test.end[:10]}",
            flush=True,
        )
    print(f"  base config: {base_path}", flush=True)
    print(f"  refdata config: {refdata_path}", flush=True)
    if args.parallel > 1:
        print(
            f"  parallel: {args.parallel} (note: backtest.sh hardcodes log paths — "
            f"concurrent cells will race; set to 1 until log isolation lands)",
            flush=True,
        )
    print(flush=True)

    rows: list[dict] = []
    import hashlib

    cell_envs = allocate_cells(args.parallel)
    rotate_cell_idx = 0  # round-robin assignment when parallel > 1

    for i, (combo, split, phase) in enumerate(cells, 1):
        # Build the params file (same as before) — strategy params don't
        # depend on walk-forward window.
        params_cfg = copy.deepcopy(base)
        label_parts = []
        for path, value in zip(paths, combo):
            deep_set(params_cfg, path, value)
            label_parts.append(short_label(path, value))
        if split:
            label_parts.append(f"split={split.index}/{phase}")
        label = " ".join(label_parts) if label_parts else f"cell-{i}"

        # Choose cell env (round-robin). Cell 0 is the legacy single-tenant
        # path so single-cell sweeps remain byte-identical to pre-Phase-4.
        cell_env = cell_envs[rotate_cell_idx]
        rotate_cell_idx = (rotate_cell_idx + 1) % args.parallel

        # Generate temp configs.
        tag = f"sweep-{i:03d}"
        params_tmp_path = params_path.parent / f"{params_path.stem.split('.')[0]}.{tag}.toml"
        params_tmp_path.write_bytes(tomli_w.dumps(params_cfg).encode())

        instance_cfg = copy.deepcopy(instance)
        instance_cfg["strategy_config"] = os.path.relpath(params_tmp_path, base_path.parent)

        # Walk-forward: replace [simulation] with the train or test window.
        # The base config may use top-level start/end OR [[simulation.windows]];
        # we overwrite both forms cleanly.
        if split:
            sim = instance_cfg.setdefault("simulation", {})
            sim.pop("start", None)
            sim.pop("end", None)
            sim.pop("windows", None)
            sim.pop("sessions", None)
            window = split.train if phase == "train" else split.test
            sim["windows"] = [window.to_toml()]

        # Cell isolation: media_driver_dir, metrics port, endpoint ports.
        instance_cfg = cell_env.apply_to_instance_cfg(instance_cfg)

        # Even in serial mode (--parallel 1, all cells use cell_env idx 0)
        # each cell still needs a unique metrics port so the strategy can
        # rebind without waiting for the previous cell's TIME_WAIT to clear.
        # Offset by 100 per cell to stay well clear of the 9101–9105 standard
        # service-port range used by refdata/md-gateway/ogw/strategy/backtester.
        if args.parallel == 1 and "metrics" in instance_cfg:
            base_port = instance_cfg["metrics"].get("port", 9104)
            instance_cfg["metrics"]["port"] = base_port + i * 100

        instance_stem = base_path.stem.split(".")[0]
        instance_tmp_path = base_path.parent / f"{instance_stem}.{tag}.toml"
        instance_tmp_path.write_bytes(tomli_w.dumps(instance_cfg).encode())

        print(f"[{i}/{n_cells}] {label}", flush=True)
        if args.dry_run:
            print(f"    instance: {instance_tmp_path}")
            print(f"    params:   {params_tmp_path}")
            if split:
                window = split.train if phase == "train" else split.test
                print(f"    window:   [{window.start}, {window.end})")
            params_tmp_path.unlink(missing_ok=True)
            instance_tmp_path.unlink(missing_ok=True)
            continue

        try:
            ok = run_one(instance_tmp_path, refdata_path)
            if not ok:
                print(f"    backtest failed — see logs", flush=True)
                continue

            params_hash_full = hashlib.sha256(instance_tmp_path.read_bytes()).hexdigest()
            run_dir = find_run_dir_by_params_hash(params_hash_full)
            if run_dir:
                summary = load_summary(run_dir) or {}
                rows.append(
                    {
                        "label": label,
                        "phase": phase,
                        "split_index": split.index if split else None,
                        "params_combo": " ".join(short_label(p, v) for p, v in zip(paths, combo)),
                        "run_id": run_dir.name,
                        "fills": summary.get("total_fills"),
                        "return_pct": summary.get("return_pct"),
                        "max_dd_pct": summary.get("max_drawdown_pct"),
                        "sharpe": summary.get("sharpe_per_fill"),
                        "win_rate": summary.get("win_rate_pct"),
                        "buy_count": summary.get("buy_count"),
                        "sell_count": summary.get("sell_count"),
                    }
                )
            else:
                print(f"    no result dir for params_hash={params_hash_full[:8]}", flush=True)
        finally:
            params_tmp_path.unlink(missing_ok=True)
            instance_tmp_path.unlink(missing_ok=True)

    if not rows:
        print("\nNo successful runs.")
        return 1

    # ── Reporting ─────────────────────────────────────────────────────────────
    print()
    if splits:
        # Walk-forward: render train + test side by side keyed on (params_combo, split_index).
        by_key: dict[tuple, dict] = {}
        for r in rows:
            key = (r["params_combo"], r["split_index"])
            by_key.setdefault(key, {})[r["phase"]] = r
        print(
            f"{'params':<32}  {'split':>5}  "
            f"{'train_ret%':>10}  {'train_fills':>11}  "
            f"{'test_ret%':>10}  {'test_fills':>11}  "
            f"{'gen_drop%':>9}"
        )
        for (combo_label, split_idx), pair in sorted(
            by_key.items(), key=lambda kv: (kv[0][0], kv[0][1] or 0)
        ):
            tr = pair.get("train") or {}
            te = pair.get("test") or {}
            tret = tr.get("return_pct")
            teret = te.get("return_pct")
            gen = (teret - tret) * 100 if (tret is not None and teret is not None) else None
            print(
                f"{combo_label[:32]:<32}  {split_idx:>5}  "
                f"{(tret or 0) * 100:>+10.3f}  {tr.get('fills') or 0:>11}  "
                f"{(teret or 0) * 100:>+10.3f}  {te.get('fills') or 0:>11}  "
                f"{gen if gen is not None else 0:>+9.3f}"
            )
    else:
        print(
            f"{'#':>3}  {'params':<40}  {'fills':>6}  {'return%':>9}  "
            f"{'maxDD%':>8}  {'sharpe':>7}  {'win%':>6}  {'B/S':>7}"
        )
        for i, r in enumerate(rows, 1):
            bs = f"{r['buy_count']}/{r['sell_count']}" if r["buy_count"] is not None else "—"
            ret = f"{(r['return_pct'] or 0) * 1.0:+.4f}" if r["return_pct"] is not None else "—"
            dd = f"{r['max_dd_pct'] or 0:.4f}" if r["max_dd_pct"] is not None else "—"
            sh = f"{r['sharpe'] or 0:+.4f}" if r["sharpe"] is not None else "—"
            wr = f"{r['win_rate'] or 0:.2f}" if r["win_rate"] is not None else "—"
            print(
                f"{i:>3}  {r['label']:<40}  {r['fills'] or 0:>6}  {ret:>9}  "
                f"{dd:>8}  {sh:>7}  {wr:>6}  {bs:>7}"
            )

    return 0


if __name__ == "__main__":
    sys.exit(main())

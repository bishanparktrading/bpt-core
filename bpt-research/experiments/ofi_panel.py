"""Cross-day OFI IC panel + multivariate AS-weight calibration.

Loops over (instrument, day) for a set of canon files. For each cell,
streams OFI through the same C++ calculator AS uses, computes 1s
forward log-return via pd.merge_asof, and records:

  - n              tick count
  - ic_ofi         univariate Spearman IC(ofi, ret_1s)
  - beta_ofi_uni   univariate OLS slope (bps per unit OFI)
  - beta_ofi_mv    multivariate OLS slope from ret_1s ~ ofi + drift
                   (the partial contribution AS should actually weight)
  - ofi_std        per-(instrument, day) std of OFI (for σ-scaling)

Aggregates per-instrument across days: IC mean / std, suggested
ofi_weight_bps_ derived from the multivariate slope on instruments
with low cross-day IC variance.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.stats import spearmanr

REPO_ROOT = Path('/home/jseow/code/bpt-core')
sys.path.insert(0, str(REPO_ROOT / 'bpt-canon' / 'python'))
sys.path.insert(0, str(REPO_ROOT / 'bpt-features' / 'python'))
sys.path.insert(0, str(REPO_ROOT / 'bazel-bin' / 'bpt-features' / 'python'))

import bpt_canon as bc
import bpt_features as bf

HORIZON_NS = 1_000_000_000  # 1 second
DRIFT_HALFLIFE_S = 30.0
OFI_WINDOW_NS = 1_000_000_000
OFI_MAX_LEVELS = 1


def _ic_and_betas(bbo_i: pd.DataFrame) -> dict:
    """Compute IC + univariate / multivariate slopes for one (instrument, day)."""
    cfg = bf.OFICalculator.Config()
    cfg.max_levels = OFI_MAX_LEVELS
    cfg.window_ns = OFI_WINDOW_NS
    calc = bf.OFICalculator(cfg)

    ofi = np.empty(len(bbo_i), dtype=float)
    for i, row in enumerate(bbo_i.itertuples(index=False)):
        ofi[i] = calc.update(
            bids=[(row.bid, row.bid_qty)],
            asks=[(row.ask, row.ask_qty)],
            timestamp_ns=row.ts_ns,
        )
    bbo_i = bbo_i.assign(ofi=ofi)
    bbo_i['drift'] = bf.ewma_drift(bbo_i, halflife_s=DRIFT_HALFLIFE_S)

    fwd = pd.merge_asof(
        pd.DataFrame({'target_ts': bbo_i.ts_ns + HORIZON_NS}),
        bbo_i[['ts_ns', 'mid']].rename(columns={'ts_ns': 'fwd_ts', 'mid': 'fwd_mid'}),
        left_on='target_ts',
        right_on='fwd_ts',
        direction='forward',
    )
    bbo_i['ret_1s'] = np.log(fwd.fwd_mid.values / bbo_i.mid.values)

    mask = bbo_i.ret_1s.notna() & bbo_i.ofi.notna() & bbo_i.drift.notna()
    df = bbo_i.loc[mask, ['ofi', 'drift', 'ret_1s']]
    n = len(df)
    if n < 500:
        return {'n': n, 'ic_ofi': np.nan, 'beta_ofi_uni': np.nan,
                'beta_ofi_mv': np.nan, 'ofi_std': np.nan}

    ic, _ = spearmanr(df.ofi, df.ret_1s)
    ofi_std = df.ofi.std()
    beta_uni = np.polyfit(df.ofi, df.ret_1s, 1)[0]
    X = np.column_stack([df.ofi, df.drift, np.ones(n)])
    coef, *_ = np.linalg.lstsq(X, df.ret_1s.values, rcond=None)
    return {'n': n, 'ic_ofi': float(ic),
            'beta_ofi_uni': float(beta_uni),
            'beta_ofi_mv': float(coef[0]),
            'ofi_std': float(ofi_std)}


def run_panel(canon_paths: list[Path]) -> pd.DataFrame:
    rows = []
    for cp in canon_paths:
        day = cp.stem  # e.g. hl-2026-05-13-h00
        bbo = bc.read_bbos(cp)
        bbo = bbo[(bbo.bid > 0) & (bbo.ask > 0) & (bbo.ask > bbo.bid)].reset_index(drop=True)
        bbo['mid'] = (bbo.bid + bbo.ask) * 0.5
        for iid, grp in bbo.groupby('instrument_id', sort=True):
            grp = grp.reset_index(drop=True)
            metrics = _ic_and_betas(grp)
            rows.append({'day': day, 'instrument_id': int(iid), **metrics})
            print(f'  {day} iid={iid}  n={metrics["n"]:>6}  ic={metrics["ic_ofi"]:+.3f}  '
                  f'β_mv={metrics["beta_ofi_mv"]:+.2e}', flush=True)
    return pd.DataFrame(rows)


def aggregate(panel: pd.DataFrame, ic_std_threshold: float = 0.08) -> pd.DataFrame:
    """Per-instrument IC mean/std + suggested ofi_weight_bps_."""
    g = panel.dropna(subset=['ic_ofi']).groupby('instrument_id')
    summary = g.agg(
        days=('day', 'nunique'),
        ic_mean=('ic_ofi', 'mean'),
        ic_std=('ic_ofi', 'std'),
        beta_mv_mean=('beta_ofi_mv', 'mean'),
        ofi_std_mean=('ofi_std', 'mean'),
    ).reset_index()
    # Suggested skew: bps of mid moved per σ-sized OFI deviation, from the
    # multivariate partial slope (drift already controlled out).
    # beta_mv is in log-return per unit OFI; × 1e4 → bps; × ofi_std → per σ.
    summary['suggested_ofi_weight_bps_per_sigma'] = (
        summary.beta_mv_mean * summary.ofi_std_mean * 1e4
    )
    summary['stable'] = summary.ic_std < ic_std_threshold
    return summary.sort_values('ic_mean', ascending=False)


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument('--canon-dir', type=Path, default=Path('/tmp/bpt_canon'))
    p.add_argument('--glob', type=str, default='hl-2026-05-*-h00.canon')
    p.add_argument('--out-panel', type=Path, default=Path('/tmp/bpt_canon/ofi_panel.csv'))
    p.add_argument('--out-summary', type=Path, default=Path('/tmp/bpt_canon/ofi_panel_summary.csv'))
    args = p.parse_args(argv)

    canon_paths = sorted(args.canon_dir.glob(args.glob))
    if not canon_paths:
        print(f'no canon files match {args.canon_dir}/{args.glob}', file=sys.stderr)
        return 1
    print(f'panel over {len(canon_paths)} canon files', flush=True)
    panel = run_panel(canon_paths)
    panel.to_csv(args.out_panel, index=False)
    summary = aggregate(panel)
    summary.to_csv(args.out_summary, index=False)
    print('\n=== per-instrument summary ===')
    print(summary.to_string(index=False))
    print(f'\nwrote {args.out_panel} and {args.out_summary}')
    return 0


if __name__ == '__main__':
    sys.exit(main())

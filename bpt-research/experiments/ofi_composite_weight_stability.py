"""K-fold per-instrument weight stability check.

Both post-fit polish levers (gate, ridge) failed to improve over OLS. The
35.7% residual loss to best-single is therefore either (a) train/test
regime drift / insufficient training data, or (b) information starvation
(OFI + microprice_dev jointly don't carry enough signal for the residual
instruments).

This script tells those apart. Leave-one-day-out CV per instrument: 10
folds, each fits weights on 9 days. Then:
  - Per-instrument: std and coefficient of variation (std/|mean|) of each
    weight across folds. Low CV = stable, high CV = data-starved.
  - Compare instruments that BEAT best-single in the parent study against
    those that LOST. If losers have higher weight variance, the answer is
    more training data. If both cohorts have similar stability, the answer
    is more (different) features.

Output: per-instrument weight std + CV; cohort comparison.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
import pandas as pd

REPO = Path('/home/jseow/code/bpt-core')
sys.path.insert(0, str(REPO / 'bpt-canon' / 'python'))
sys.path.insert(0, str(REPO / 'bpt-features' / 'python'))
sys.path.insert(0, str(REPO / 'bazel-bin' / 'bpt-features' / 'python'))
sys.path.insert(0, str(REPO / 'bpt-research'))

import bpt_canon as bc  # noqa: E402
from ic import ofi, microprice_dev  # noqa: E402
from ic.panel import _prepare_bbo, _forward_return  # noqa: E402
from ic.multivariate import _z  # noqa: E402

HORIZON_NS = 1_000_000_000
MIN_TICKS = 500


def gather(canon_paths: list[Path]) -> dict[int, list[pd.DataFrame]]:
    by_iid: dict[int, list[pd.DataFrame]] = {}
    for cp in canon_paths:
        bbo = _prepare_bbo(bc.read_bbos(cp))
        for iid, grp in bbo.groupby('instrument_id', sort=True):
            grp = grp.reset_index(drop=True)
            ret = _forward_return(grp, HORIZON_NS).values
            f_o = ofi(grp).values
            f_m = microprice_dev(grp).values
            mask = ~np.isnan(ret) & ~np.isnan(f_o) & ~np.isnan(f_m)
            if int(mask.sum()) < MIN_TICKS:
                continue
            by_iid.setdefault(int(iid), []).append(pd.DataFrame({
                'ofi': f_o[mask], 'microprice_dev': f_m[mask],
                'ret_1s': ret[mask], 'day': cp.stem,
            }))
    return by_iid


def fit_per_instrument_ols(frames: list[pd.DataFrame]) -> dict[str, float]:
    zs_o = np.concatenate([_z(df.ofi.values) for df in frames])
    zs_m = np.concatenate([_z(df.microprice_dev.values) for df in frames])
    Y = np.concatenate([df.ret_1s.values for df in frames])
    X = np.column_stack([zs_o, zs_m, np.ones(len(Y))])
    coef, *_ = np.linalg.lstsq(X, Y, rcond=None)
    return {'ofi': float(coef[0]), 'microprice_dev': float(coef[1])}


def main() -> int:
    all_canons = sorted(Path('/tmp/bpt_canon').glob('hl-2026-05-*-h00.canon'))
    print(f'canons: {len(all_canons)}', flush=True)
    by = gather(all_canons)
    print(f'instruments: {len(by)}', flush=True)

    # Per-instrument: must have a frame for each of the 10 days for clean LOO
    n_days = len(all_canons)
    full_cov = {iid: frames for iid, frames in by.items() if len(frames) == n_days}
    print(f'instruments with full 10-day coverage: {len(full_cov)}', flush=True)

    # Leave-one-day-out per instrument
    print('\nrunning LOO CV per instrument...', flush=True)
    rows = []
    for iid, frames in full_cov.items():
        weights_per_fold = []
        for held_out_idx in range(n_days):
            train = [f for i, f in enumerate(frames) if i != held_out_idx]
            w = fit_per_instrument_ols(train)
            weights_per_fold.append(w)
        ofi_w = np.array([w['ofi'] for w in weights_per_fold])
        mp_w = np.array([w['microprice_dev'] for w in weights_per_fold])

        # Stability metrics
        rows.append({
            'instrument_id': iid,
            'w_ofi_mean':  float(ofi_w.mean()),
            'w_ofi_std':   float(ofi_w.std(ddof=1)),
            'w_ofi_cv':    float(abs(ofi_w.std(ddof=1) / ofi_w.mean()))
                            if ofi_w.mean() != 0 else float('inf'),
            'w_mp_mean':   float(mp_w.mean()),
            'w_mp_std':    float(mp_w.std(ddof=1)),
            'w_mp_cv':     float(abs(mp_w.std(ddof=1) / mp_w.mean()))
                            if mp_w.mean() != 0 else float('inf'),
            # Sign-flip rate: how often does each weight flip sign across folds?
            'ofi_sign_flips':  int(((np.sign(ofi_w[:-1]) * np.sign(ofi_w[1:])) < 0).sum()),
            'mp_sign_flips':   int(((np.sign(mp_w[:-1])  * np.sign(mp_w[1:]))  < 0).sum()),
        })
    df = pd.DataFrame(rows)

    # Load parent study's per-instrument result to split winners/losers
    parent_csv = Path('/tmp/bpt_canon/per_inst_composite.csv')
    if not parent_csv.exists():
        print(f'WARN: {parent_csv} missing — re-running per_inst study first '
              'would give the cohort split. Continuing without cohort split.', flush=True)
        cohort_df = df.copy()
        cohort_df['cohort'] = 'unknown'
    else:
        parent = pd.read_csv(parent_csv)
        # Winners: per-instrument composite > best_single. Losers: < best_single.
        parent['cohort'] = np.where(parent.uplift_vs_best_single > 0, 'winner', 'loser')
        cohort_df = df.merge(parent[['instrument_id', 'cohort', 'symbol',
                                      'uplift_vs_best_single', 'ic_per_inst',
                                      'best_single']],
                              on='instrument_id', how='left')

    with open(REPO / 'config/instruments/instrument_mapping.hyperliquid-mainnet.json') as f:
        iid2sym = {v: k.replace('3_', '') for k, v in json.load(f)['forward'].items()}
    cohort_df['symbol'] = cohort_df['instrument_id'].map(iid2sym)
    cohort_df.to_csv('/tmp/bpt_canon/composite_weight_stability.csv', index=False)

    # Cohort comparison — the key diagnostic
    print('\n=== Cohort comparison: weight stability ===')
    if 'cohort' in cohort_df.columns and cohort_df.cohort.notna().any():
        for cohort in ['winner', 'loser']:
            sub = cohort_df[cohort_df.cohort == cohort]
            if len(sub) == 0:
                continue
            print(f'\n  {cohort.upper()}S — {len(sub)} instruments')
            for col in ['w_ofi_std', 'w_ofi_cv', 'w_mp_std', 'w_mp_cv',
                        'ofi_sign_flips', 'mp_sign_flips']:
                v = sub[col].replace([np.inf, -np.inf], np.nan).dropna()
                print(f'    {col:18s}  median={v.median():.4g}  mean={v.mean():.4g}')

    # Headline counts: how many instruments have sign-flipping weights?
    print('\n=== Sign-flip diagnostic ===')
    print(f'  instruments with w_ofi sign-flips > 0:  '
          f'{(cohort_df.ofi_sign_flips > 0).sum()}/{len(cohort_df)}')
    print(f'  instruments with w_mp  sign-flips > 0:  '
          f'{(cohort_df.mp_sign_flips > 0).sum()}/{len(cohort_df)}')

    # Show example winners and losers with their stability
    print('\n=== Sample WINNERS (per-instrument composite beat best-single) ===')
    win = cohort_df[cohort_df.cohort == 'winner'].sort_values('ic_per_inst', ascending=False).head(8)
    print(win[['symbol', 'w_ofi_mean', 'w_ofi_cv', 'w_mp_mean', 'w_mp_cv',
                'ofi_sign_flips', 'mp_sign_flips', 'uplift_vs_best_single']]
              .to_string(index=False, float_format=lambda v: f'{v:.4g}'))

    print('\n=== Sample LOSERS (lost to best-single) ===')
    lose = cohort_df[cohort_df.cohort == 'loser'].sort_values('uplift_vs_best_single').head(8)
    print(lose[['symbol', 'w_ofi_mean', 'w_ofi_cv', 'w_mp_mean', 'w_mp_cv',
                 'ofi_sign_flips', 'mp_sign_flips', 'uplift_vs_best_single']]
              .to_string(index=False, float_format=lambda v: f'{v:.4g}'))

    print('\nwrote /tmp/bpt_canon/composite_weight_stability.csv')
    return 0


if __name__ == '__main__':
    sys.exit(main())

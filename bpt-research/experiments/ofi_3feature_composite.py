"""Three-feature per-instrument composite: OFI + microprice_dev + trade_flow_ewma.

Stability diagnostic concluded the 35.7% residual loss in the 2-feature
per-instrument composite is information starvation, not data starvation.
This script tests the natural fix: add trade_flow_ewma (signed taker
volume EWMA) as a third predictor.

Smoke test on 1 day showed trade_flow_ewma's pairwise Spearman with OFI
and microprice is ~0.10-0.18 — much lower than the OFI/microprice
overlap (~0.45-0.51 on liquid instruments). Individual IC is lower than
OFI/microprice (+0.04-0.12 vs +0.34-0.48 on the SOL/ETH/AAVE check)
but if the information is independent, OLS will still give it a non-
trivial weight on instruments where it adds signal.

Comparison set per instrument:
  - OFI alone (best of {OFI, microprice, trade_flow} as best-single baseline)
  - Microprice alone
  - Trade-flow alone
  - 2-feature per-instrument composite (yesterday's +0.157 result)
  - 3-feature per-instrument composite ← the test

7-day train / 3-day test split as before.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.stats import spearmanr

REPO = Path('/home/jseow/code/bpt-core')
sys.path.insert(0, str(REPO / 'bpt-canon' / 'python'))
sys.path.insert(0, str(REPO / 'bpt-features' / 'python'))
sys.path.insert(0, str(REPO / 'bazel-bin' / 'bpt-features' / 'python'))
sys.path.insert(0, str(REPO / 'bpt-research'))

import bpt_canon as bc  # noqa: E402

from ic import ofi, microprice_dev, trade_flow_ewma  # noqa: E402
from ic.panel import _prepare_bbo, _forward_return  # noqa: E402
from ic.multivariate import _z  # noqa: E402

HORIZON_NS = 1_000_000_000
MIN_TICKS = 500
TRADE_FLOW_HALFLIFE_S = 1.0


def gather(canon_paths: list[Path]) -> dict[int, list[pd.DataFrame]]:
    by_iid: dict[int, list[pd.DataFrame]] = {}
    for cp in canon_paths:
        bbo = _prepare_bbo(bc.read_bbos(cp))
        trades = bc.read_trades(cp)
        for iid, grp in bbo.groupby('instrument_id', sort=True):
            grp = grp.reset_index(drop=True)
            ret = _forward_return(grp, HORIZON_NS).values
            f_ofi = ofi(grp).values
            f_mp = microprice_dev(grp).values
            trades_i = trades[trades.instrument_id == iid].reset_index(drop=True)
            f_tf = trade_flow_ewma(grp, trades_i,
                                   halflife_s=TRADE_FLOW_HALFLIFE_S).values
            mask = (~np.isnan(ret) & ~np.isnan(f_ofi)
                    & ~np.isnan(f_mp) & ~np.isnan(f_tf))
            if int(mask.sum()) < MIN_TICKS:
                continue
            by_iid.setdefault(int(iid), []).append(pd.DataFrame({
                'ofi':            f_ofi[mask],
                'microprice_dev': f_mp[mask],
                'trade_flow':     f_tf[mask],
                'ret_1s':         ret[mask],
                'day':            cp.stem,
            }))
    return by_iid


def fit_per_instrument(train_frames: list[pd.DataFrame],
                        features: list[str]) -> dict[str, float]:
    """Pool train rows, z-score per-cell, fit OLS for the named features."""
    zs = {f: [] for f in features}
    rets = []
    for df in train_frames:
        for f in features:
            zs[f].append(_z(df[f].values))
        rets.append(df.ret_1s.values)
    Z = np.column_stack([np.concatenate(zs[f]) for f in features])
    Y = np.concatenate(rets)
    X = np.column_stack([Z, np.ones(len(Y))])
    coef, *_ = np.linalg.lstsq(X, Y, rcond=None)
    return {features[i]: float(coef[i]) for i in range(len(features))}


def score_composite(test_frames: list[pd.DataFrame],
                     weights: dict[str, float]) -> float:
    sigs, rets = [], []
    for df in test_frames:
        s = np.zeros(len(df))
        for f, w in weights.items():
            s += w * _z(df[f].values)
        sigs.append(s); rets.append(df.ret_1s.values)
    S = np.concatenate(sigs); Y = np.concatenate(rets)
    valid = ~np.isnan(S) & ~np.isnan(Y)
    if int(valid.sum()) < 100:
        return float('nan')
    rho, _ = spearmanr(S[valid], Y[valid])
    return float(rho)


def score_single(test_frames: list[pd.DataFrame], col: str) -> float:
    S = np.concatenate([_z(df[col].values) for df in test_frames])
    Y = np.concatenate([df.ret_1s.values for df in test_frames])
    valid = ~np.isnan(S) & ~np.isnan(Y)
    if int(valid.sum()) < 100:
        return float('nan')
    rho, _ = spearmanr(S[valid], Y[valid])
    return float(rho)


def pairwise_corrs(train_frames: list[pd.DataFrame]) -> dict[str, float]:
    """Within-cell pooled Spearman pairwise correlations across all instruments."""
    pooled = pd.concat(train_frames, ignore_index=True)
    out = {}
    for a, b in [('ofi','microprice_dev'),
                 ('ofi','trade_flow'),
                 ('microprice_dev','trade_flow')]:
        rho, _ = spearmanr(pooled[a], pooled[b])
        out[f'{a} vs {b}'] = float(rho)
    return out


def main() -> int:
    all_canons = sorted(Path('/tmp/bpt_canon').glob('hl-2026-05-*-h00.canon'))
    train, test = all_canons[:7], all_canons[7:]
    print(f'train: {len(train)}d, test: {len(test)}d', flush=True)

    print('gathering train (BBO + trades + features per cell)...', flush=True)
    train_by = gather(train)
    print(f'  train instruments: {len(train_by)}', flush=True)
    print('gathering test...', flush=True)
    test_by = gather(test)
    print(f'  test instruments:  {len(test_by)}', flush=True)

    common = sorted(set(train_by) & set(test_by))
    print(f'instruments with both train + test: {len(common)}', flush=True)

    with open(REPO / 'config/instruments/instrument_mapping.hyperliquid-mainnet.json') as f:
        iid2sym = {v: k.replace('3_', '') for k, v in json.load(f)['forward'].items()}

    # Pooled pairwise correlations across the whole train set
    print('\n=== Pooled pairwise feature correlations (train) ===')
    all_train_frames = [df for iid in common for df in train_by[iid]]
    for k, v in pairwise_corrs(all_train_frames).items():
        print(f'  {k:42s}  Spearman = {v:+.4f}')

    rows = []
    for iid in common:
        sym = iid2sym.get(iid, str(iid))
        tr = train_by[iid]
        te = test_by[iid]
        # Individual feature ICs on test
        ic_ofi   = score_single(te, 'ofi')
        ic_mp    = score_single(te, 'microprice_dev')
        ic_tf    = score_single(te, 'trade_flow')
        best_single = max(ic_ofi, ic_mp, ic_tf)

        # 2-feature composite (OFI + microprice)
        w2 = fit_per_instrument(tr, ['ofi', 'microprice_dev'])
        ic_2feat = score_composite(te, w2)

        # 3-feature composite
        w3 = fit_per_instrument(tr, ['ofi', 'microprice_dev', 'trade_flow'])
        ic_3feat = score_composite(te, w3)

        rows.append({
            'symbol': sym, 'instrument_id': iid,
            'w_ofi': w3['ofi'],
            'w_mp': w3['microprice_dev'],
            'w_tf': w3['trade_flow'],
            'ic_ofi': ic_ofi,
            'ic_mp': ic_mp,
            'ic_tf': ic_tf,
            'best_single': best_single,
            'ic_2feat': ic_2feat,
            'ic_3feat': ic_3feat,
            'uplift_3_vs_2': ic_3feat - ic_2feat,
            'uplift_3_vs_best': ic_3feat - best_single,
        })
    df = pd.DataFrame(rows)
    df.to_csv('/tmp/bpt_canon/composite_3feature.csv', index=False)

    # Aggregate headlines
    print('\n=== Out-of-sample mean IC, all instruments ===')
    for col in ['ic_ofi', 'ic_mp', 'ic_tf', 'ic_2feat', 'ic_3feat']:
        v = df[col].dropna()
        print(f'  {col:10s}  mean={v.mean():+.4f}  median={v.median():+.4f}  '
              f'pos_frac={(v>0).mean():.1%}')

    wins_3v2 = (df.uplift_3_vs_2 > 0).sum()
    wins_3vbest = (df.uplift_3_vs_best > 0).sum()
    print(f'\n  3-feat beats 2-feat composite:  '
          f'{wins_3v2}/{len(df)} ({100*wins_3v2/len(df):.1f}%)  '
          f'mean uplift = {df.uplift_3_vs_2.mean():+.4f}')
    print(f'  3-feat beats best-single:       '
          f'{wins_3vbest}/{len(df)} ({100*wins_3vbest/len(df):.1f}%)  '
          f'mean uplift = {df.uplift_3_vs_best.mean():+.4f}')

    print('\n=== Top 15 by 3-feat uplift over 2-feat ===')
    top = df.sort_values('uplift_3_vs_2', ascending=False).head(15)
    print(top[['symbol', 'w_ofi', 'w_mp', 'w_tf',
                'ic_2feat', 'ic_3feat', 'uplift_3_vs_2']]
              .to_string(index=False, float_format=lambda v: f'{v:.4f}'))

    print('\n=== Bottom 15 (3-feat HURT vs 2-feat) ===')
    bot = df.sort_values('uplift_3_vs_2').head(15)
    print(bot[['symbol', 'w_ofi', 'w_mp', 'w_tf',
                'ic_2feat', 'ic_3feat', 'uplift_3_vs_2']]
              .to_string(index=False, float_format=lambda v: f'{v:.4f}'))

    # Save & done
    print('\nwrote /tmp/bpt_canon/composite_3feature.csv')
    return 0


if __name__ == '__main__':
    sys.exit(main())

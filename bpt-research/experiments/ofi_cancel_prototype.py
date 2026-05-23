"""OFI-cancellation prototype simulation.

Built on top of the shadow-markout study. That study showed pre-trade OFI
predicts adverse markout — +1.26 bps median save per cancelled fill at
1σ gate. This script asks the strictly stronger question: across a
threshold sweep, what is the AGGREGATE realized PnL impact of the rule
(not just per-cancel save)?

Per-cancel save and aggregate PnL differ because:
  - Higher θ → fewer cancellations, larger per-cancel save (only the
    most extreme OFI cases get cancelled, and those are most predictive)
  - Lower θ → more cancellations, smaller per-cancel save (less extreme
    cases get cancelled too, where the prediction is weaker)
  - Aggregate PnL = (fills that happen) × markout. Cancellation reduces
    both fill count and (one hopes) per-fill markout.

The optimal θ is the one that maximises aggregate PnL per instrument.
This isn't obvious from the per-cancel save figure alone.

Simulation model:
  - AS is *always* quoting both sides at touch (max simplification).
  - Each canon trade is a hypothetical fill on the opposite-side quote.
  - Cancellation rule: cancel the about-to-be-hit side when OFI predicts
    adverse flow toward that side.
  - PnL on each fill = signed markout × qty (negative = adverse).

Output: per-(instrument, θ) realized markout-PnL totals;
aggregate sweep curve (mean across instruments).
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
from ic import ofi  # noqa: E402
from ic.panel import _prepare_bbo  # noqa: E402

MARKOUT_HORIZON_NS = 1_000_000_000  # 1s
MIN_TRADES_PER_INST = 200

# Threshold sweep in units of per-instrument OFI std.
# θ = 0   → cancel any time OFI sign predicts adverse (= always cancel one side)
# θ = ∞   → never cancel = baseline
SIGMA_THRESHOLDS = [0.0, 0.5, 1.0, 1.5, 2.0, 3.0, float('inf')]


def collect_for_instrument(bbo_i: pd.DataFrame,
                            trades_i: pd.DataFrame) -> pd.DataFrame:
    """Returns one row per trade: side, ofi_pre, markout_1s, qty."""
    if len(trades_i) == 0 or len(bbo_i) < 100:
        return pd.DataFrame()

    ofi_at_bbo = ofi(bbo_i).values
    bbo_ts = bbo_i['ts_ns'].values
    bbo_mid = bbo_i['mid'].values

    trades_i = trades_i[trades_i['side'] < 2].reset_index(drop=True)
    if len(trades_i) == 0:
        return pd.DataFrame()
    trade_ts = trades_i['ts_ns'].values

    bbo_idx = np.searchsorted(bbo_ts, trade_ts, side='right') - 1
    valid = bbo_idx >= 0
    pre_trade_ofi = np.where(valid, ofi_at_bbo[bbo_idx], np.nan)
    anchor_mid = np.where(valid, bbo_mid[bbo_idx], np.nan)

    future_ts = trade_ts + MARKOUT_HORIZON_NS
    fut_idx = np.searchsorted(bbo_ts, future_ts, side='left')
    fut_valid = fut_idx < len(bbo_ts)
    fut_mid = np.where(fut_valid,
                        bbo_mid[np.minimum(fut_idx, len(bbo_ts) - 1)],
                        np.nan)
    markout = np.log(fut_mid / anchor_mid) * 1e4  # bps

    return pd.DataFrame({
        'side': trades_i['side'].values,
        'qty': trades_i['qty'].values,
        'pre_trade_ofi': pre_trade_ofi,
        'anchor_mid': anchor_mid,
        'markout_1s_bps': markout,
    }).dropna(subset=['pre_trade_ofi', 'markout_1s_bps'])


def simulate(trades: pd.DataFrame, theta: float, ofi_std: float) -> dict:
    """Apply cancellation rule + measure realized PnL.

    PnL convention: AS as maker.
      - BUY trade: AS sold its ask → PnL per unit = -markout (bps)
      - SELL trade: AS bought bid → PnL per unit = +markout (bps)
    (markout is post-trade mid return; positive markout = adverse for AS-as-seller.)
    """
    pos_gate = theta * ofi_std
    neg_gate = -theta * ofi_std

    # cancel ask (skip BUY fill) when OFI > pos_gate
    # cancel bid (skip SELL fill) when OFI < neg_gate
    buy_mask  = (trades.side == 0)
    sell_mask = (trades.side == 1)

    if theta == float('inf'):
        cancelled = pd.Series(False, index=trades.index)
    else:
        cancelled = (
            (buy_mask  & (trades.pre_trade_ofi > pos_gate)) |
            (sell_mask & (trades.pre_trade_ofi < neg_gate))
        )

    kept = trades[~cancelled]
    # PnL in bps × qty per fill
    buy_kept = kept[kept.side == 0]
    sell_kept = kept[kept.side == 1]
    pnl_buy = -(buy_kept.markout_1s_bps * buy_kept.qty).sum()
    pnl_sell = (sell_kept.markout_1s_bps * sell_kept.qty).sum()

    return {
        'theta_sigma': theta,
        'n_trades_total': len(trades),
        'n_cancelled': int(cancelled.sum()),
        'cancel_rate': float(cancelled.mean()) if len(trades) else 0.0,
        'n_filled': int(len(kept)),
        'pnl_bps_qty': float(pnl_buy + pnl_sell),
        'pnl_per_filled_bps_qty':
            float((pnl_buy + pnl_sell) / max(len(kept), 1)),
        'mean_markout_filled_bps':
            float(kept.markout_1s_bps.abs().mean()) if len(kept) else 0.0,
    }


def main() -> int:
    all_canons = sorted(Path('/tmp/bpt_canon').glob('hl-2026-05-*-h00.canon'))
    print(f'canons: {len(all_canons)}', flush=True)

    rows = []
    for cp in all_canons:
        bbo = _prepare_bbo(bc.read_bbos(cp))
        trades = bc.read_trades(cp)
        for iid, grp in bbo.groupby('instrument_id', sort=True):
            grp = grp.reset_index(drop=True)
            tr_i = trades[trades.instrument_id == iid].reset_index(drop=True)
            if len(tr_i) < MIN_TRADES_PER_INST:
                continue
            df = collect_for_instrument(grp, tr_i)
            if df.empty:
                continue
            df['instrument_id'] = int(iid)
            df['day'] = cp.stem
            rows.append(df)
        print(f'  {cp.stem}: {sum(len(r) for r in rows)} pooled trades so far',
              flush=True)

    panel = pd.concat(rows, ignore_index=True)
    print(f'\ntotal trades pooled: {len(panel):,}  '
          f'instruments: {panel.instrument_id.nunique()}', flush=True)

    with open(REPO / 'config/instruments/instrument_mapping.hyperliquid-mainnet.json') as f:
        iid2sym = {v: k.replace('3_', '') for k, v in json.load(f)['forward'].items()}

    # Per-instrument threshold sweep
    sim_rows = []
    for iid, grp in panel.groupby('instrument_id'):
        ofi_std = grp.pre_trade_ofi.std()
        if ofi_std == 0 or len(grp) < MIN_TRADES_PER_INST:
            continue
        baseline_pnl = simulate(grp, float('inf'), ofi_std)['pnl_bps_qty']
        for theta in SIGMA_THRESHOLDS:
            res = simulate(grp, theta, ofi_std)
            res['symbol'] = iid2sym.get(iid, str(iid))
            res['instrument_id'] = iid
            res['ofi_std'] = ofi_std
            res['baseline_pnl_bps_qty'] = baseline_pnl
            res['pnl_uplift_bps_qty'] = res['pnl_bps_qty'] - baseline_pnl
            sim_rows.append(res)
    sims = pd.DataFrame(sim_rows)
    sims.to_csv('/tmp/bpt_canon/ofi_cancel_prototype.csv', index=False)

    # Aggregate sweep across instruments
    print('\n=== Aggregate sweep — mean across instruments ===')
    print(f'{"θ(σ)":>6s}  {"cancel%":>8s}  {"pnl_bps_qty":>14s}  '
          f'{"vs_baseline":>13s}  {"win_rate":>9s}', flush=True)
    for theta in SIGMA_THRESHOLDS:
        sub = sims[sims.theta_sigma == theta]
        if len(sub) == 0: continue
        wins = (sub.pnl_uplift_bps_qty > 0).sum()
        print(f'  {theta:>4.1f}  {sub.cancel_rate.mean()*100:>7.2f}%  '
              f'{sub.pnl_bps_qty.mean():>+14.2f}  '
              f'{sub.pnl_uplift_bps_qty.mean():>+13.3f}  '
              f'{wins}/{len(sub)} ({100*wins/len(sub):>5.1f}%)',
              flush=True)

    # Pick best per-instrument θ
    print('\n=== Per-instrument optimal threshold (best PnL uplift) ===')
    best_by_iid = (
        sims.loc[sims.groupby('instrument_id')['pnl_uplift_bps_qty'].idxmax()]
            .reset_index(drop=True)
    )
    best_by_iid.to_csv('/tmp/bpt_canon/ofi_cancel_prototype_best.csv', index=False)
    pos_uplift = (best_by_iid.pnl_uplift_bps_qty > 0).sum()
    print(f'  instruments with positive uplift (at SOME θ): '
          f'{pos_uplift}/{len(best_by_iid)} ({100*pos_uplift/len(best_by_iid):.1f}%)')
    print(f'  median optimal uplift: '
          f'{best_by_iid.pnl_uplift_bps_qty.median():+.3f}  '
          f'mean: {best_by_iid.pnl_uplift_bps_qty.mean():+.3f}')

    # Top winners
    print('\n=== Top 12 instruments by realized PnL uplift ===')
    top = best_by_iid.sort_values('pnl_uplift_bps_qty', ascending=False).head(12)
    print(top[['symbol', 'theta_sigma', 'cancel_rate',
                'baseline_pnl_bps_qty', 'pnl_bps_qty', 'pnl_uplift_bps_qty']]
              .to_string(index=False, float_format=lambda v: f'{v:.2f}'))

    print('\n=== Bottom 12 instruments ===')
    bot = best_by_iid.sort_values('pnl_uplift_bps_qty').head(12)
    print(bot[['symbol', 'theta_sigma', 'cancel_rate',
                'baseline_pnl_bps_qty', 'pnl_bps_qty', 'pnl_uplift_bps_qty']]
              .to_string(index=False, float_format=lambda v: f'{v:.2f}'))

    print('\nwrote /tmp/bpt_canon/ofi_cancel_prototype{,_best}.csv')
    return 0


if __name__ == '__main__':
    sys.exit(main())

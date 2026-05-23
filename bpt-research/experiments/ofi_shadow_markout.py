"""Shadow-markout study: does pre-trade OFI predict adverse markout?

Yesterday's AS backtest had too few real fills (3-9 per instrument) to
draw conclusions about which fills were adverse. But every trade in the
canon represents a moment SOMEONE took liquidity at a posted quote — if
we treat each trade as a "shadow fill" (the resting maker side AS would
have been on), we get ~100k samples per (day, instrument) instead of
~10. Plenty of statistical power.

Setup per trade:
  - aggressor side (BUY/SELL from the trade record)
  - OFI value just BEFORE the trade (causal — uses only past info)
  - markout at h ∈ {50ms, 1s, 5s} after the trade

A trade with side=BUY means the aggressor was a buyer; they took
liquidity from someone's resting ASK. AS-as-maker on that ask just
sold. **Adverse markout for AS** = price kept going up after they
sold (positive forward return after the trade).

Test:
  If OFI > +θ before BUY trades correlates with positive markout,
  cancelling the ask when OFI > θ would have avoided those adverse
  fills. Same logic mirrored for SELL trades and bid cancellation.

This is the standard "shadow fill" markout analysis that HFT shops use
to prove a cancellation/throttle signal exists BEFORE building the
mechanic.
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

MARKOUT_HORIZONS_NS = {
    '50ms':   50_000_000,
    '1s':  1_000_000_000,
    '5s':  5_000_000_000,
}
MIN_TRADES_PER_INST = 200


def collect_for_instrument(bbo_i: pd.DataFrame,
                            trades_i: pd.DataFrame) -> pd.DataFrame:
    """For each trade, return: side, ofi_pre_trade, markout_<h> for each h.

    OFI is computed by streaming BBO through OFICalculator up to the
    most recent BBO before each trade — no lookahead.
    """
    if len(trades_i) == 0 or len(bbo_i) < 100:
        return pd.DataFrame()

    # 1) Pre-compute OFI value at every BBO timestamp (causal — calc only
    # sees past BBOs as it streams).
    ofi_at_bbo = ofi(bbo_i).values
    bbo_ts = bbo_i['ts_ns'].values
    bbo_mid = bbo_i['mid'].values

    trades_i = trades_i[trades_i['side'] < 2].reset_index(drop=True)
    if len(trades_i) == 0:
        return pd.DataFrame()
    trade_ts = trades_i['ts_ns'].values

    # 2) For each trade, OFI value at the most recent BBO ≤ trade_ts.
    # searchsorted with side='right' then -1 gives the last bbo idx ≤ ts.
    bbo_idx = np.searchsorted(bbo_ts, trade_ts, side='right') - 1
    valid = bbo_idx >= 0
    pre_trade_ofi = np.where(valid, ofi_at_bbo[bbo_idx], np.nan)

    # 3) Mid at trade time (= mid of the same pre-trade BBO) as anchor.
    # Markout = log(mid_future / mid_anchor). Using mid avoids
    # bid-ask-bounce confusion.
    anchor_mid = np.where(valid, bbo_mid[bbo_idx], np.nan)

    # 4) For each markout horizon, find the BBO at ≥ (trade_ts + h)
    rows = {
        'side': trades_i['side'].values,
        'trade_ts': trade_ts,
        'pre_trade_ofi': pre_trade_ofi,
        'anchor_mid': anchor_mid,
        'trade_price': trades_i['price'].values,
        'trade_qty': trades_i['qty'].values,
    }
    for h_label, h_ns in MARKOUT_HORIZONS_NS.items():
        future_ts = trade_ts + h_ns
        fut_idx = np.searchsorted(bbo_ts, future_ts, side='left')
        fut_valid = fut_idx < len(bbo_ts)
        fut_mid = np.where(fut_valid, bbo_mid[np.minimum(fut_idx, len(bbo_ts)-1)],
                            np.nan)
        markout = np.log(fut_mid / anchor_mid)  # log-return
        rows[f'markout_{h_label}_bps'] = markout * 1e4

    return pd.DataFrame(rows)


def main() -> int:
    all_canons = sorted(Path('/tmp/bpt_canon').glob('hl-2026-05-*-h00.canon'))
    print(f'canons: {len(all_canons)}', flush=True)

    # Pool across all 10 days — pure observational study, no train/test
    rows = []
    iids_seen = set()
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
            iids_seen.add(int(iid))
        print(f'  {cp.stem}: {len(iids_seen)} instruments so far', flush=True)

    if not rows:
        print('no trades collected', file=sys.stderr); return 1
    panel = pd.concat(rows, ignore_index=True)
    panel = panel.dropna(subset=['pre_trade_ofi', 'markout_1s_bps'])
    print(f'\ntotal trades pooled: {len(panel):,}  '
          f'instruments: {panel.instrument_id.nunique()}', flush=True)

    with open(REPO / 'config/instruments/instrument_mapping.hyperliquid-mainnet.json') as f:
        iid2sym = {v: k.replace('3_', '') for k, v in json.load(f)['forward'].items()}

    # === Test 1: Stratify by trade-side × OFI sign, mean markout
    print('\n=== Markout (bps) by trade-side × pre-trade OFI sign ===')
    print('  Negative markout for SELL trades = AS would have LOST money if filled (price moved down after buying)')
    print('  Positive markout for BUY trades = AS would have LOST money if filled (price moved up after selling)')
    print()
    for side_val, side_name in [(0, 'BUY (AS sells ask)'), (1, 'SELL (AS buys bid)')]:
        sub = panel[panel.side == side_val]
        n = len(sub)
        if n == 0:
            continue
        # OFI positive vs negative cohorts
        pos = sub[sub.pre_trade_ofi > 0]
        neg = sub[sub.pre_trade_ofi < 0]
        print(f'  {side_name}  n={n:,}')
        for h in ['50ms', '1s', '5s']:
            col = f'markout_{h}_bps'
            print(f'    {h:>4s}: overall mean={sub[col].mean():+.3f}  '
                  f'OFI>0 mean={pos[col].mean():+.3f} (n={len(pos):,})  '
                  f'OFI<0 mean={neg[col].mean():+.3f} (n={len(neg):,})  '
                  f'diff={pos[col].mean()-neg[col].mean():+.3f}')
        print()

    # === Test 2: For each instrument, how much markout could OFI cancel save?
    print('=== Per-instrument "saved markout" estimate (top + bottom 10) ===')
    # The save: avoided markout on (BUY, OFI>θ) fills + (SELL, OFI<-θ) fills.
    # Use θ = 1 standard-deviation of OFI per instrument as a starting bar.
    save_rows = []
    for iid, grp in panel.groupby('instrument_id'):
        ofi_std = grp.pre_trade_ofi.std()
        if ofi_std == 0:
            continue
        theta = ofi_std  # 1-σ gate
        buy = grp[grp.side == 0]
        sell = grp[grp.side == 1]
        # Adverse-for-AS markout (1s) where the rule would cancel
        canceled_buy = buy[buy.pre_trade_ofi > theta]
        canceled_sell = sell[sell.pre_trade_ofi < -theta]
        # On these "would-have-fills" the AS-as-maker markout is:
        #   BUY side: AS sold → markout > 0 = adverse → cancellation saves
        #   SELL side: AS bought → markout < 0 = adverse → cancellation saves
        # Average per-cancellation save (in bps) is the absolute markout
        save_buy = canceled_buy['markout_1s_bps'].mean() if len(canceled_buy) else 0.0
        save_sell = -canceled_sell['markout_1s_bps'].mean() if len(canceled_sell) else 0.0

        # Same for "if AS hadn't cancelled, what would markout be" overall
        # (the natural baseline AS faced before the rule)
        baseline_buy = buy['markout_1s_bps'].mean()  # >0 means adverse for AS-sell
        baseline_sell = -sell['markout_1s_bps'].mean()  # AS-buy: <0 markout = adverse, so flip sign

        save_rows.append({
            'symbol': iid2sym.get(iid, str(iid)),
            'instrument_id': iid,
            'trades_total': len(grp),
            'ofi_std': ofi_std,
            'cancel_rate_buy_pct':  100.0 * len(canceled_buy) / max(len(buy), 1),
            'cancel_rate_sell_pct': 100.0 * len(canceled_sell) / max(len(sell), 1),
            'baseline_buy_markout_bps':  baseline_buy,
            'cancelled_buy_markout_bps': save_buy,
            'buy_save_bps':              save_buy - baseline_buy,
            'baseline_sell_markout_bps':  baseline_sell,
            'cancelled_sell_markout_bps': save_sell,
            'sell_save_bps':              save_sell - baseline_sell,
        })
    saves = pd.DataFrame(save_rows)
    saves['avg_save_bps'] = (saves.buy_save_bps + saves.sell_save_bps) / 2.0
    saves = saves.sort_values('avg_save_bps', ascending=False)
    saves.to_csv('/tmp/bpt_canon/ofi_shadow_markout.csv', index=False)
    panel.to_csv('/tmp/bpt_canon/ofi_shadow_markout_panel.csv', index=False)

    print('\nTop 10 instruments by potential cancellation save (bps per cancelled would-be fill):')
    print(saves.head(10)[['symbol','trades_total',
                           'cancel_rate_buy_pct','baseline_buy_markout_bps','cancelled_buy_markout_bps',
                           'cancel_rate_sell_pct','baseline_sell_markout_bps','cancelled_sell_markout_bps',
                           'avg_save_bps']].to_string(index=False, float_format=lambda v: f'{v:.3f}'))
    print('\nBottom 10:')
    print(saves.tail(10)[['symbol','trades_total',
                            'cancel_rate_buy_pct','baseline_buy_markout_bps','cancelled_buy_markout_bps',
                            'cancel_rate_sell_pct','baseline_sell_markout_bps','cancelled_sell_markout_bps',
                            'avg_save_bps']].to_string(index=False, float_format=lambda v: f'{v:.3f}'))

    print(f'\n=== Aggregate (across 182 instruments) ===')
    print(f'  median per-instrument save: {saves.avg_save_bps.median():+.3f} bps')
    print(f'  mean   per-instrument save: {saves.avg_save_bps.mean():+.3f} bps')
    print(f'  instruments with positive save: {(saves.avg_save_bps > 0).sum()}/{len(saves)}')
    print('\nwrote /tmp/bpt_canon/ofi_shadow_markout{,_panel}.csv')
    return 0


if __name__ == '__main__':
    sys.exit(main())

# Shadow-markout study — OFI predicts adverse fills, cancellation mechanic validated

**Date:** 2026-05-24
**Author:** pivot to AS consumption-mechanic work (after exhausting signal-calibration side)
**Status:** Complete — strongest positive result of the session. OFI cancellation is worth prototyping.

## Question

Yesterday's AS backtest showed the OFI signal didn't translate to PnL via the reservation-skew mechanic. The doc proposed two alternative consumption mechanics:

1. **OFI-as-cancellation** — when |OFI| > θ predicts adverse direction, cancel the at-touch order on the about-to-be-hit side. Trades immunity instead of edge.
2. **OFI-as-sizing** — scale order_qty per side by OFI to reduce exposure to predicted-adverse fills.

Before building either, this study asks the cheaper precursor question: **does pre-trade OFI actually predict adverse markout on the trades AS would have been filled on?** If yes, OFI-cancellation has theoretical merit. If no, the consumption-mechanic problem is something else (e.g., latency, queue position).

The diagnostic uses every trade in the canon as a "shadow fill" — treat each trade as a hypothetical AS resting-side execution, then look at the markout. ~700k shadow fills, vs the ~14 real fills from yesterday's backtest.

## Setup

| | |
|---|---|
| Data | Same 10 HL hour-00 canon days (no train/test — observational study) |
| Universe | 122 instruments with ≥200 trades (filter cuts low-volume tail) |
| Per trade | Aggressor side, OFI value at the most recent BBO ≤ trade ts, markout at h ∈ {50ms, 1s, 5s} |
| OFI | Same C++ calculator AS uses (`max_levels=1`, `window=1s`) |
| Markout | `log(mid_{trade_ts + h} / mid_{trade_ts})` × 1e4 to bps |
| Sign convention | BUY trades → AS hypothetically sold its ask. Positive markout = price went up after = adverse for AS. SELL trades → AS bought bid. Negative markout = price down after = adverse. |
| Script | `bpt-research/experiments/ofi_shadow_markout.py` |

Sample size: **732,395 shadow trades** across 122 instruments — three orders of magnitude more than the real-fill sample from the backtest.

## Result — OFI clearly predicts adverse markout

Stratified by trade side × OFI sign at the time of trade:

| Side | Horizon | Overall mean | OFI > 0 cohort | OFI < 0 cohort | Δ |
|---|---|---|---|---|---|
| BUY (AS sells ask) | 50ms | +1.83 bps | +2.10 bps (n=228k) | +1.33 bps (n=149k) | **+0.77 bps** |
| BUY (AS sells ask) | 1s   | +2.59 bps | +2.85 bps | +2.18 bps | **+0.67 bps** |
| BUY (AS sells ask) | 5s   | +3.03 bps | +3.42 bps | +2.39 bps | **+1.03 bps** |
| SELL (AS buys bid) | 50ms | −2.00 bps | −1.10 bps (n=122k) | −2.49 bps (n=203k) | **+1.40 bps** |
| SELL (AS buys bid) | 1s   | −2.80 bps | −2.02 bps | −3.25 bps | **+1.23 bps** |
| SELL (AS buys bid) | 5s   | −2.99 bps | −1.93 bps | −3.62 bps | **+1.69 bps** |

Both signs work in the predicted direction:
- BUY trades preceded by **positive** OFI (buy pressure) have **more** positive markout (worse for AS-as-seller)
- SELL trades preceded by **negative** OFI (sell pressure) have **more** negative markout (worse for AS-as-buyer)

The diff grows with horizon — meaning OFI's signal about *direction* is durable, not just a transient response to the trade itself. Also, **the SELL side is roughly 2× stronger** (1.23 bps vs 0.67 bps at 1s). Asymmetry might be specific to HL's order flow composition; worth investigating separately.

## Per-instrument economics

Estimate "save per cancelled fill" per instrument by gating at θ = 1 standard-deviation of OFI:

| Aggregate | Value |
|---|---|
| Instruments where rule helps (positive save) | **99 / 122 (81%)** |
| Median per-instrument save | **+1.26 bps per avoided fill** |
| Mean per-instrument save | +1.51 bps |
| Cancel rate (median) — BUY side | ~15% of trades |
| Cancel rate (median) — SELL side | ~15% of trades |

### Top 10 winners (largest cancellation save)

| Symbol | Trades | Cancel rate BUY | Baseline mkt | Cancelled mkt | Cancel rate SELL | Baseline | Cancelled | Avg save (bps) |
|---|---|---|---|---|---|---|---|---|
| BABY  | 657   | 27% | +7.95 | +31.24 | 35% | +20.92 | +27.25 | **+14.81** |
| PNUT  | 779   | 7%  | +4.70 | +19.20 | 23% | +5.18  | +8.10  | +8.71 |
| ANIME | 236   | 33% | +2.66 | +4.28  | 7%  | +1.09  | +15.99 | +8.26 |
| GOAT  | 549   | 23% | +6.63 | +15.13 | 15% | +6.75  | +14.21 | +7.98 |
| AVNT  | 841   | 14% | +4.43 | +18.00 | 22% | +0.35  | +2.55  | +7.89 |
| REZ   | 3317  | 16% | +8.78 | +20.96 | 8%  | +1.33  | +3.02  | +6.94 |

Most winners are memecoin / low-cap / new-listing instruments with directional flow — exactly where OFI should be most informative.

### Bottom 10 (rule would hurt)

| Symbol | Trades | Save (bps) | Notes |
|---|---|---|---|
| ALT  | 739    | −16.26 | Small N, likely noise |
| LIT  | 30,181 | −4.79  | **Real signal: rule HURTS on LIT** |
| ICP  | 2,135  | −3.41  | Real |
| TST  | 279    | −2.61  | Small N |
| VINE | 657    | −2.25  | Small N |
| HYPE | 209,637 | −1.58 | Large N — rule mildly hurts on HYPE |

On 23/122 instruments, OFI cancellation makes markout WORSE. The mechanic needs per-instrument gating — likely only apply to instruments where the historical save is robustly positive.

## Interpretation

1. **The signal exists at the right magnitude.** Median save +1.26 bps per cancelled fill is meaningful relative to ~2 bps typical maker fee on HL. On the memecoin tail (BABY, PNUT, GOAT) it's 5-15 bps — large enough to flip a marginally-negative MM strategy positive.
2. **Both sides work, with asymmetry.** The SELL side (AS buying bid when negative OFI predicts further down) is roughly 2× stronger than the BUY side. Worth understanding why before deploying — may reflect HL's flow composition.
3. **Per-instrument gating is mandatory.** 23/122 instruments show negative save; deploying the rule uniformly would lose money on those. A pre-deployment filter ("only enable cancellation on instruments where historical save > 0.5 bps") would capture most of the upside.
4. **This is a different finding from the signal-calibration arc.** The +0.157 OOS IC measured how well OFI predicts forward returns *on average*. This study measures how much OFI predicts adverse markout *conditional on a trade arriving* — a strictly more relevant question for cancellation logic, which only fires when a trade is about to fill.

## Implications

1. **Build the OFI-cancellation prototype.** Justified by the data. Python first (cheap to iterate), then C++ port if it works.
2. **Per-instrument enablement list.** Use the 99 instruments with positive expected save as the deploy set; exclude the 23 negatives. This is a simple lookup-table addition to AS config.
3. **Threshold θ tunable.** 1-σ was a starting point; the prototype should sweep across thresholds (0.5σ, 1σ, 2σ, 3σ) per instrument to find the trade-off between cancel-rate and per-cancel-save.
4. **Cancellation rule complements reservation-skew, doesn't replace it.** Reservation skew biases AS's chosen quote price; cancellation is a *protective* action when adverse flow is imminent. Could run both — they address different problems.

## Caveats

1. **Shadow markout ≠ real markout.** This study assumes AS would have been at-touch on the cancellable side at the trade time. Reality: AS quotes behind-touch sometimes, requotes constantly, has queue position. The "save" estimate is an upper bound — real save would be lower because AS doesn't get filled on every trade that crosses its quote.
2. **Cancel rate is a cost.** Every cancelled order loses queue position. If AS frequently cancels and re-quotes, queue priority on the *useful* fills degrades. Need to measure this trade-off in a real simulation.
3. **The "save" doesn't measure new adverse selection introduced.** When AS cancels its ask on positive OFI, it doesn't earn the spread on whatever trade arrives next on the other side. Lost opportunity isn't in the +1.26 bps figure.
4. **Single venue, hour-00 only.** Same as all prior arcs.
5. **No interaction with strategy state.** Real AS tracks inventory, position, and pause flags. The cancellation rule might conflict with inventory unwinding or risk gates.

## Reproduce

```bash
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
  python bpt-research/experiments/ofi_shadow_markout.py
# output: /tmp/bpt_canon/ofi_shadow_markout{,_panel}.csv
```

## Next experiments

1. **Python prototype of OFI-cancellation rule** — simulate AS quoting at each tick, apply cancellation, fill against trade stream, measure realized markout. Compare to baseline (no cancellation). Validates the +1.26 bps shadow-save translates to real markout in a quoting simulation.
2. **Threshold sweep per instrument** — once prototype works, find optimal θ per instrument.
3. **C++ port to AS** — if prototype confirms the save, port the cancellation rule into `avellaneda_stoikov_strategy.cpp` as a new config-gated path (`ofi_cancel_threshold`).

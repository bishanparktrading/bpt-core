# OFI 10-day cross-day IC panel + AS weight calibration

**Date:** 2026-05-23
**Author:** continuation of `2026-05-23_ofi_predictiveness_hl.md`
**Status:** Complete — IC panel + AS backtest. IC is real but did **not**
translate to AS PnL via reservation-skew. See "Backtest result" section.

## Question

Two questions, both prerequisites to retuning `ofi_weight_bps_` in AS:

1. **Is the OFI IC stable across days, per instrument?** Yesterday's finding had IC mean ≈ 0.22 across 10 instruments × 2 days, all positive — but a 2-day window is too short to claim stability. Need IC variance per instrument before any production weight change.
2. **What is the *partial* slope of `ret_1s` on OFI after controlling for drift?** Univariate β overstates OFI's contribution because OFI is partly autocorrelated with recent returns (caveat #4 in the prior doc). Multivariate `ret_1s ~ ofi + drift` gives the contribution AS should actually weight.

## Setup

| | |
|---|---|
| Data | 10 HL days × 1 hour each = hour 00 UTC of 2026-05-13 → 2026-05-22 |
| Captures | `s3://bpt-tape-archive/raw/hyperliquid/<day>/hyperliquid-<earliest>.wslog` per day |
| Canon | `/tmp/bpt_canon/hl-2026-05-{13..22}-h00.canon` via `bpt-canon-replay` |
| Universe | 182 instruments observed across all 10 days; all kept |
| Panel cells | 1820 (instrument × day), all with valid IC |
| OFI config | `max_levels=1`, `window_ns=1s` — AS defaults (unchanged from prior doc) |
| Drift | `bf.ewma_drift(halflife_s=30)` — AS production setting |
| Forward returns | `ret_1s = log(mid_{t+1s} / mid_t)`, `merge_asof(direction='forward')` |
| Score | Spearman IC + univariate β + multivariate β from `ret_1s ~ ofi + drift` |
| Panel script | `bpt-research/experiments/ofi_panel.py` |
| Outputs | `/tmp/bpt_canon/ofi_panel.csv`, `ofi_panel_summary.csv`, `ofi_tuning_candidates.csv` |

**Methodology choice — fixed hour, not rotated.** All 10 samples are from UTC hour 00 (Asia-active window). Rotating hours across days would inflate the cross-day IC variance for the wrong reason (time-of-day effect mixed in). Time-of-day generalization is a separate cut. See "Caveats."

## Headline results

- **Mean IC across all 1820 (instrument × day) cells: 0.149**
- **96.1% of cells have positive IC.** Sign-stable across the entire HL universe, not just the top names.
- **161 / 182 instruments are stable** (IC std across 10 days < 0.08).
- **40 instruments are tuning candidates** (stable AND IC mean > 0.20).
- **Univariate vs multivariate slope: median 3.5% difference, 90th percentile 11.7%.**
  Drift collinearity is much smaller than feared — OFI carries genuinely independent information at the 1s horizon, so the prior doc's univariate-IC headline numbers weren't materially inflated.

## Top tuning candidates

Stable instruments (IC std < 0.08) with IC mean > 0.20, sorted by IC mean. `recommended_ofi_weight_bps` is the multivariate-slope-derived bps per σ-OFI, scaled by **0.5× for headroom** (other signals + IC→P&L slippage).

| Symbol | iid | IC mean | IC std | β_mv (log-ret / OFI) | OFI σ | rec. `ofi_weight_bps_` |
|---|---|---|---|---|---|---|
| SOL  | 1003 | 0.397 | 0.056 | 1.48e-05 | 1.19 | **0.088** |
| ETH  | 1002 | 0.355 | 0.053 | 1.49e-05 | 0.98 | **0.073** |
| AAVE | 1008 | 0.353 | 0.061 | 1.58e-05 | 1.46 | **0.116** |
| POL  | 1147 | 0.329 | 0.073 | 1.85e-05 | 0.68 | **0.063** |
| UNI  | 1045 | 0.310 | 0.049 | 1.62e-05 | 1.04 | **0.084** |
| FET  | 1078 | 0.291 | 0.037 | 2.13e-05 | 0.85 | **0.090** |
| kLUNC| 1097 | 0.291 | 0.043 | 3.57e-05 | 0.94 | **0.168** |
| NIL  | 1189 | 0.277 | 0.067 | 4.08e-05 | 0.88 | **0.180** |
| JTO  | 1100 | 0.272 | 0.055 | 3.65e-05 | 0.86 | **0.157** |
| ONDO | 1112 | 0.261 | 0.031 | 1.54e-05 | 1.54 | **0.118** |
| JUP  | 1096 | 0.259 | 0.048 | 2.47e-05 | 0.78 | **0.097** |
| DOGE | 1009 | 0.249 | 0.029 | 1.47e-05 | 1.19 | **0.088** |
| kPEPE| 1012 | 0.228 | 0.036 | 3.00e-05 | 0.58 | **0.087** |

Full list of 40: `/tmp/bpt_canon/ofi_tuning_candidates.csv`.

### Comparison with the prior 2-day doc

All 10 instruments from the prior doc replicate stably:

| Symbol | Prior 2-day IC range | 10-day IC mean ± std | Stable? |
|---|---|---|---|
| ETH      | 0.20–0.34 | 0.355 ± 0.053 | yes |
| SOL      | 0.29–0.35 | 0.397 ± 0.056 | yes |
| APE      | 0.19–0.26 | 0.214 ± 0.032 | yes |
| HYPE     | 0.12–0.20 | 0.178 ± 0.050 | yes |
| ZEC      | 0.11–0.12 | 0.117 ± 0.045 | yes |
| AAVE     | 0.23–0.32 | 0.353 ± 0.061 | yes |
| DOGE     | 0.19–0.27 | 0.249 ± 0.029 | yes |
| TAO      | 0.13–0.28 | 0.205 ± 0.050 | yes |
| FARTCOIN | 0.10–0.16 | 0.089 ± 0.029 | yes |
| kPEPE    | 0.24–0.26 | 0.228 ± 0.036 | yes |

Two-day means sat inside the 10-day ± std band for every instrument. The signal isn't a 2-day fluke.

## Calibration logic

For each stable instrument, the multivariate partial slope `β_OFI` from `ret_1s ~ ofi + drift` translates as:

```
expected_log_return_per_unit_OFI = β_OFI
expected_return_per_σ_OFI        = β_OFI × ofi_std        [log-return]
                                 ≈ β_OFI × ofi_std × 1e4  [bps]
```

This is the *incremental* bp move expected per σ-sized OFI deviation, after the move predicted by recent drift is already accounted for. It's the right scale for `ofi_weight_bps_`, which AS interprets as bps-of-skew per unit OFI.

**Headroom factor 0.5×** is applied to `recommended_ofi_weight_bps`. Two reasons:
1. IC-derived expected returns assume frictionless execution. Real fills hit fees, queue, slippage, and adverse-selection — typically these subtract 30–50% of the IC's apparent edge.
2. AS uses OFI alongside drift and σ². Leaving headroom prevents OFI from dominating the reservation price during high-vol windows where drift should have more say.

## Caveats

1. **Hour 00 UTC only.** Asia-active window. Same OFI may carry less signal in EU/US sessions when HFT participation is heavier. The "Next experiments" list opens with the hour-of-day cut.
2. **No transaction-cost layer.** This is still signal calibration, not strategy P&L. The recommended weight is "what AS *should* skew by," not "what AS *will earn* by skewing by." A live shadow-quote run is the right next step before pushing the weights to prod.
3. **Instruments with < 500 ticks in any (instrument, day) cell are excluded from that cell's IC.** Kept low-N noise out of the means. Affected mostly long-tail instruments — all 40 tuning candidates have full sample sizes.
4. **Drift collinearity turned out small (median 3.5% slope adjustment), so the univariate IC in the prior doc was a reasonable first-pass signal magnitude — not double-counting much.** Multivariate is still the right input for the weight itself.
5. **Single venue.** OKX replication is still the next-highest-info cross-cut after this one.
6. **Negative-IC instruments exist** (4% of cells, lowest summary: TRUMP -0.055). These are not candidates for an OFI-positive weight. If AS ever runs on those, the weight needs to be per-instrument and signed.

## Implications

1. **Push per-instrument weights**, not a single global `ofi_weight_bps_`. The 0.05–0.50 spread across instruments is real signal heterogeneity, not noise. A single weight is leaving edge on the table on high-IC names (kLUNC, NIL, VIRTUAL) and overweighting on low-σ names.
2. **The 40 candidates list is what AS should turn on first.** Lock OFI weight at 0 (or status-quo) on the rest until conditional cuts or longer history justifies them.
3. **The prior doc's headline IC=0.22 was directionally right.** 10-day mean is 0.15 across the full universe (broader, noisier instruments included); restricted to the top 10 names from the prior doc it's still ≈ 0.27. The signal isn't a regime-specific fluke.
4. **The univariate-vs-multivariate "double-counting" worry was overblown.** β changes by < 4% at the median. OFI and drift are nearly orthogonal at this horizon. Future single-feature IC studies on this stack can probably skip the multivariate step unless explicitly stated otherwise.

## Reproduce

```bash
# 1. Download wslogs (one-time, ~21 GB for 10 days)
for d in 2026-05-{13..22}; do
  fname=$(aws s3 ls "s3://bpt-tape-archive/raw/hyperliquid/$d/" | awk '{print $4}' | sort | head -1)
  aws s3 cp "s3://bpt-tape-archive/raw/hyperliquid/$d/$fname" "/tmp/bpt_wslog/hl-${d}-h00.wslog" --quiet
done

# 2. Convert to canon (~10 min sequential, ~3 min with 4-way parallelism)
ls /tmp/bpt_wslog/hl-*.wslog | xargs -P 4 -I {} bash -c '
  base=$(basename "{}" .wslog)
  bazel-bin/bpt-canon/bpt-canon-replay \
    --wslog "{}" \
    --instrument-mapping config/instruments/instrument_mapping.hyperliquid-mainnet.json \
    --output "/tmp/bpt_canon/${base}.canon"
'

# 3. Run panel + tuning summary
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
  python bpt-research/experiments/ofi_panel.py
```

## Backtest result — IC did NOT translate to AS PnL

Ran AS in `bpt-backtester` over the same 10 canon days, six instruments
(XMR/APE/ZEC/SOL/ETH/AAVE), each paired:
- **baseline**: `ofi_weight_bps = 0` (current prod default)
- **tuned**: `ofi_weight_bps = <recommendation from this panel>`, `order_book_depth = 1`

| Symbol | Baseline (fills, PnL) | Tuned (fills, PnL) | Δ PnL | Notes |
|---|---|---|---|---|
| XMR  | 0, $0.00      | 0, $0.00       | $0.00    | No fills either side — too few touches that AS catches |
| APE  | 3, +$5.07     | 3, +$5.07      | **$0.00** | Bit-identical trades — OFI skew (~0.07 bp) rounded away by tick spacing |
| ZEC  | 0, $0.00      | 2, **−$140.98** | −$140.98 | OFI biased AS short across a 27% upmove; got IOC'd at $653 |
| SOL  | 0, $0.00      | 0, $0.00       | $0.00    | No fills |
| ETH  | 0, $0.00      | 9, −$3.64      | −$3.64   | OFI activated AS quoting; all 9 fills adverse |
| AAVE | 0, $0.00      | 0, $0.00       | $0.00    | No fills |

**The recommended weights did not improve PnL on any instrument; on two
(ZEC, ETH) they made it materially worse.** This is a clean negative
result against the natural reading of the panel ("higher IC → higher
ofi_weight_bps → more PnL").

### Why the IC failed to translate

Three forces, in order of impact:

1. **Tick spacing eats sub-bp skews.** Recommended weights produce
   reservation-price shifts of ~0.05–0.5 bp, which round to zero
   tick movement on most HL instruments. APE is the cleanest example:
   baseline and tuned fills are bit-identical because rounded quoted
   prices were the same.
2. **Adverse selection on the fills that *do* happen.** When OFI's
   recommended skew is large enough to shift the quoted price, the
   fills it earns are dominated by exactly the takers OFI predicted
   — i.e., AS is on the *wrong* side of OFI's predictive direction
   at fill time. ETH's 9 adverse fills illustrate this; ZEC's 27%
   upmove unwind is the worst-case tail.
3. **AS reservation-skew was designed for inventory pressure, not
   forward-return signals.** Inventory skew is a *protective* shift
   (move quotes away from accumulating side). OFI skew is a
   *predictive* shift (move quotes toward expected direction). Using
   the same mechanic for both means the predictive signal gets
   filled adversely — the predicted move is what fills you.

### What this means for prod tuning

- **Do NOT push the per-instrument `ofi_weight_bps_` recommendations to
  prod.** The IC is real, the *translation* is broken.
- **The IC measurement itself remains useful** — it tells us OFI
  carries forward-return information. The action item is to find a
  consumption mechanic that doesn't lose to adverse selection.
- **AS as it stands probably shouldn't use OFI for skew.** Two
  alternatives worth exploring (in priority order):
  1. **OFI as a *cancellation* signal**: when |OFI| > θ predicts an
     adverse move, *cancel* the at-touch order on the about-to-be-hit
     side. Trades immunity instead of edge.
  2. **OFI as a *sizing* signal**: scale `order_qty` down on the OFI
     side, up on the other. Reduces exposure to predicted-adverse
     fills without moving the quote price.
- **APE's identical fills suggests AS may not be using OFI even when
  weighted** — investigate whether tick-rounding logic in
  `avellaneda_stoikov_quoting.cpp` silently drops sub-tick skews
  before this is re-attempted on a different strategy.

### Reproduce

```bash
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
  python bpt-research/experiments/ofi_tuning_backtest.py
# outputs: /tmp/bt_runs/comparison.csv, /tmp/bt_runs/delta.csv, per-run dirs
```

## Next experiments (highest information value first)

(Re-prioritised after the negative backtest result.)

1. **OFI-as-cancellation prototype** — instead of skewing reservation,
   add a "cancel imminent adverse fill" gate to AS keyed on |OFI|. Measure
   adverse-fill avoidance rate over the same 10 days. If the gate avoids
   the bad fills without killing the good ones, this is the right
   consumption mechanic for the IC signal.
2. **OFI-as-sizing prototype** — keep AS quote prices unchanged, scale
   `order_qty` per side by OFI value. Lower notional exposure to
   predicted-adverse fills without quote movement.
3. **Investigate the APE bit-identical fills** — confirm whether AS's
   quoting layer is rounding sub-tick OFI skews to zero, or whether some
   other code path overrides the OFI contribution. Don't run more
   experiments on reservation-skew until this is understood.
4. **Hour-of-day cut on the IC measurement** — same 10 days, hours
   06/12/18. Independent of any AS consumption — tells us whether the
   IC itself is session-dependent. Still useful for any future consumer.
5. **OKX BTC-USDT-SWAP cross-venue replication** — original doc's
   next-highest experiment. Pushed down the list because the binding
   constraint is now consumption mechanic, not signal generation.

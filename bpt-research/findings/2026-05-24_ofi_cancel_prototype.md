# OFI-cancellation prototype — mechanic validated, magnitude needs spread-aware simulator

**Date:** 2026-05-24
**Author:** follow-up to `2026-05-24_shadow_markout_ofi_cancel.md`
**Status:** Complete — mechanic confirmed (cancellation reduces per-fill adverse markout monotonically). Next step: spread-aware simulator OR C++ AS port.

## Question

The shadow-markout diagnostic showed pre-trade OFI predicts adverse markout on the trades AS would have been filled on. That was a per-cancelled-fill calculation (+1.26 bps median save).

This prototype asks the stronger AGGREGATE question: across a threshold sweep θ ∈ {0, 0.5σ, 1σ, 1.5σ, 2σ, 3σ, ∞}, what is the per-fill markout improvement when the cancellation rule is applied? Does aggressive cancellation actually help, or does the larger number of cancellations dilute the per-cancel save?

## Setup

| | |
|---|---|
| Data | 10 HL hour-00 canon days, 122 instruments (≥200 trades each) |
| Sample | 732,395 trades pooled across days |
| Simulator | At each trade, decide if AS would have cancelled the about-to-be-hit side based on OFI at the most recent BBO before the trade |
| Cancellation rule | Cancel ask when OFI > +θσ. Cancel bid when OFI < −θσ. θ in instrument-σ units. |
| Per-instrument θσ | θ × per-instrument OFI std |
| Markout | log(mid_{trade+1s} / mid_{trade}) × 1e4 to bps |
| Per-fill PnL proxy | Mean absolute markout on KEPT fills (lower = better for AS-as-maker) |
| Script | `bpt-research/experiments/ofi_cancel_prototype.py` |

**Simplifying assumption:** AS is *always* at touch on both sides; every trade is a hypothetical fill on the opposite-side quote. Real AS gets touched on only a fraction of trades. So this is an *upper bound* on the volume of fills, but the per-fill markout figures are interpretable — they're per-trade averages.

## Result

Per-fill mean absolute markout, across 122 instruments:

| θ (σ) | Cancel rate | Per-fill markout (bps) | Save vs baseline | % instruments helped |
|---|---|---|---|---|
| **0.0** (most aggressive) | 56.1% | 3.36 | **+0.67** | 86.1% |
| **0.5** | 27.6% | 3.52 | +0.50 | **88.5% (peak)** |
| 1.0 | 15.1% | 3.70 | +0.33 | 79.5% |
| 1.5 | 7.1%  | 3.91 | +0.12 | 75.4% |
| 2.0 | 3.2%  | 4.00 | +0.03 | 64.8% |
| 3.0 | 0.9%  | 4.03 | 0.00 | 42.6% |
| **∞** (no cancel) | 0% | 4.03 | 0.00 | – |

**Monotonic improvement with cancellation aggressiveness.** Per-fill markout drops from 4.03 bps (baseline) to 3.36 bps (θ=0). The cancellation rule works as predicted — and the more it fires, the more markout it saves *per remaining fill*.

The two threshold candidates with strongest cases:

- **θ=0 (best mean save):** +0.67 bps/fill, 56% cancel rate, helps 86% of instruments
- **θ=0.5σ (best win rate):** +0.50 bps/fill, 28% cancel rate, helps 88.5% of instruments

θ=0.5σ is more robust (highest fraction of instruments improved) and more practical (28% cancel rate vs 56% — less queue position churn). θ=0 is the highest-EV per fill if your instrument-level historical save is positive.

## What we can and can't conclude

**Can conclude (with confidence):**
- The OFI-cancellation mechanic measurably reduces per-fill adverse markout.
- Magnitude is +0.5 to +0.7 bps per kept fill at sensible thresholds.
- ~85-89% of instruments benefit at θ ∈ {0, 0.5σ}; remaining ~15% need to be excluded from the rule.
- The directionality matches the shadow-markout diagnostic.

**Can NOT conclude (yet):**
- **Net AS PnL impact.** The prototype doesn't model the spread that AS earns on each fill. Cancellation reduces fills → reduces adverse markout (good) AND reduces spread revenue (bad). Net depends on the ratio of spread/fill to adverse-markout/fill.
- **HL maker spread is ~2 bps; adverse markout we measured is ~3-4 bps.** A 0.5-0.7 bps adverse-fill saving is meaningful — on the same order as the spread itself — but the sign of the *net* effect (after subtracting missed spread revenue) requires modelling spread.
- **Real queue position cost.** Cancellation loses queue priority on the cancelled side. Real AS pays this even when the next trade is non-adverse and would have produced spread revenue.

## Implications

1. **The mechanic is real and worth productionising.** Across 86-89% of instruments, simple OFI-sign-based cancellation reduces per-fill adverse markout by 0.5-0.7 bps. This is the largest single result of the session.
2. **Threshold = 0.5σ is the right operating point** for a first deployment: highest win rate (88.5%), modest 28% cancel rate, +0.50 bps/fill save.
3. **Per-instrument exclusion list required.** 14% of instruments lose at any threshold. These need to be marked "do not enable cancellation."
4. **Two next-step options for proving net PnL:**
   - **Spread-aware Python simulator** — add spread revenue per fill, simulate proper net PnL. ~2 hours.
   - **C++ AS port** — wire the cancellation rule into `avellaneda_stoikov_strategy.cpp` as a config-gated path, run through bpt-backtester. Realistic queue/match model. ~half-day.

The C++ port is the higher-fidelity answer but the bigger commitment. The Python sim is the faster check. Either is defensible.

## Caveats

1. **Simulator simplifications:** AS is always at touch; every trade is a hypothetical fill. Real AS quotes behind-touch, gets filled on a fraction of trades. The volumes don't transfer; the per-fill markout figures are more robust.
2. **No spread revenue:** see "Can NOT conclude" above. This is the biggest single gap.
3. **No queue position cost:** cancellation loses queue priority. Not modelled.
4. **OFI-sign-only at θ=0:** the most aggressive rule cancels based on sign of OFI alone, with no magnitude threshold. At that rate (56% cancellations), AS is essentially passive on the OFI-favored side for half the session. May conflict with inventory unwinding logic.
5. **Hour-00 UTC only, single venue.** Same as all prior arcs.

## Reproduce

```bash
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
  python bpt-research/experiments/ofi_cancel_prototype.py
# output: /tmp/bpt_canon/ofi_cancel_prototype{,_best}.csv
```

## Next experiments (highest information value first)

1. **Spread-aware simulator** — add a configurable `maker_spread_bps` parameter; PnL = (spread × fills) − (adverse_markout × fills). Re-run the threshold sweep. Tells us the *net* PnL impact of cancellation in different fee/spread regimes. Cheap (~2 hours), high information value.
2. **C++ AS port** — add `ofi_cancel_threshold_sigma` config parameter to `avellaneda_stoikov_strategy.cpp`. When |OFI| > θ × σ, cancel the about-to-be-hit side. Wire into the existing tick handler. Run through `bpt-backtester` with the 5-day train + 5-day test split from earlier. High fidelity but slower iteration.
3. **Asymmetric thresholds** — the shadow-markout study showed SELL-side adverse markout is ~2× the BUY side. Try cancelling sell-side bid at lower θ than buy-side ask. May lift the win rate further.

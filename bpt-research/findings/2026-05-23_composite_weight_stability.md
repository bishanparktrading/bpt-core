# Per-instrument weight stability — diagnostic resolves "more data vs more features"

**Date:** 2026-05-23
**Author:** follow-up to `2026-05-23_composite_ridge_negative.md`
**Status:** Complete — diagnostic conclusive. Information starvation, not data starvation. Next move: third feature.

## Question

Both gate (sparsity) and ridge (shrinkage) failed to lift the per-instrument composite above OLS. That ruled out post-fit polish as the lever for the 35.7% residual loss to best-single. Two hypotheses remained:

- **(a) Data starvation** — 7 days isn't enough training; per-instrument weights are unstable across windows. If true, the fix is more training data.
- **(b) Information starvation** — OFI + microprice_dev jointly don't carry enough signal for the residual instruments. If true, the fix is more (different) features.

This script tells them apart via leave-one-day-out CV per instrument: 10 folds × 9-day OLS fits. If weights swing across folds, that's evidence for (a). If weights are stable but composite still loses on test, that's evidence for (b).

## Setup

| | |
|---|---|
| Data | Same 10 HL hour-00 canon days |
| Universe | 182 instruments with full 10-day coverage |
| Folds | Leave-one-day-out per instrument (10 folds each) |
| Model | OLS per fold: `ret_1s ~ z(ofi) + z(microprice_dev) + const`, weights per fold |
| Metrics | std, coefficient-of-variation (std/|mean|), sign-flip count for each weight |
| Cohorts | Winners (117 instruments — per-instrument composite > best-single on test) vs losers (65 — composite < best-single) |
| Script | `bpt-research/experiments/ofi_composite_weight_stability.py` |

## Result — weights are stable in both cohorts

| Metric | Winners (117) | Losers (65) |
|---|---|---|
| w_ofi CV (median) | 5.5% | 5.6% |
| w_ofi CV (mean) | 6.1% | 7.6% |
| w_mp CV (median) | 7.9% | 8.9% |
| w_mp CV (mean) | 9.8% | 20.1% |
| w_ofi sign flips | 0 / 117 | 0 / 65 |
| w_mp sign flips | 0 / 117 | 5 / 65 |

**Median CV is single-digit % in both cohorts.** Across 10 leave-one-out folds, per-instrument weights vary by ~5–9% — meaning 7 days of training already produces stable weights. Adding more training data would not materially change them.

The mean CV is slightly higher for losers on the microprice weight (20% vs 10%), but that's driven by a small tail: 5 instruments with sign-flipping microprice weights. **The median — which represents the typical loser — is essentially identical to the winners' median.**

### Looking at specific losers

| Symbol | w_ofi CV | w_mp CV | Sign flips | Uplift vs best-single |
|---|---|---|---|---|
| FTT  | 24%  | **114%** | 2 (mp) | −0.398 |
| LTC  | 4.6% | 7.9%  | 0 | −0.092 |
| BNB  | 5.1% | 9.1%  | 0 | −0.080 |
| CRV  | 4.5% | 8.2%  | 0 | −0.067 |
| INJ  | 5.8% | 8.7%  | 0 | −0.056 |
| RESOLV | 4.4% | 4.0% | 0 | −0.047 |
| LINK | 3.5% | 6.1%  | 0 | −0.045 |
| BSV  | 18%  | 8.3%  | 0 | −0.044 |

**FTT is the only genuine instability case.** Most losers (LTC, BNB, CRV, INJ, RESOLV, LINK) have rock-solid weights — CV < 10% — yet still lose IC to best-single. Stable wrong-magnitude weights can't be fixed by more training data, because more training data converges to the same weights.

## Diagnosis

**Information starvation, not data starvation.**

- Hypothesis (a) — more data — would require unstable weights as evidence. **Rejected.** Weights are stable.
- Hypothesis (b) — more features — is supported by elimination. With OFI + microprice fully fit, the composite still loses on 65 instruments. The optimal 2-feature signal is just *not enough information* for these instruments.

The single FTT-style exception (genuinely unstable weights) suggests a small subset of instruments would benefit from longer training windows or being excluded from the per-instrument fit, but they're a side concern, not the main story.

## Implications

1. **The only remaining IC-uplift lever is adding features.** Both post-fit polish (gate, ridge) and more training data have been ruled out by their respective negatives.
2. **The new feature needs low correlation to both OFI and microprice.** OFI/microprice cell-Spearman is +0.33 — already too overlapping. A feature with similar overlap would just dilute the signal.
3. **Natural candidates** (lowest expected correlation with OFI/microprice first):
   - **Trade-flow EWMA** (signed taker volume over a window) — captures *aggressing-side bias* distinct from OFI's *resting-side imbalance*.
   - **Order-arrival rate** (cancels + new orders per second) — captures *book activity* distinct from book *imbalance*.
   - **Queue imbalance** (top-of-book qty asymmetry) — closer to microprice; lower expected lift.
4. **For the FTT-style unstable subset**, consider an "exclusion" rule: instruments with |CV| > 50% on any weight get treated as "use OFI alone" since the composite fit isn't reliable for them.

## Caveats

1. **CV is one stability metric of many.** Could also check pairwise weight correlation across folds, or weight prediction error on held-out days. CV alone is enough for this diagnostic but not the full picture.
2. **Leave-one-day-out folds share most of their training data** (9/10 days overlap between any two). That biases CV downward — the *real* week-to-week variance might be a touch higher than what LOO shows. Doesn't change the conclusion (the cohorts look similar).
3. **The 7-day to 9-day jump is small.** A larger-window stability check (30 days → 27-day LOO folds) would be the proper test for "could a much longer training window stabilise things." Skipped because the LOO result is already clear-cut and getting 30 days requires more canon production.

## Reproduce

```bash
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
  python bpt-research/experiments/ofi_composite_weight_stability.py
# output: /tmp/bpt_canon/composite_weight_stability.csv
```

## Next experiments (highest information value first — REVISED again)

1. **Trade-flow EWMA as a third feature** — first candidate. If it adds independent information (low pairwise correlation with OFI/microprice and meaningful per-instrument IC), the 3-feature per-instrument composite is the natural next study.
2. **Pairwise correlation check first** — before building the trade-flow EWMA feature into the regression, confirm its Spearman correlation with OFI and microprice is < 0.3. If it's > 0.5, it won't add much.
3. **Order-arrival rate** if (1) doesn't pan out.
4. **Long-window stability re-check** (30+ days) — only if more-data turns out to matter after all (e.g. someone disputes the LOO-bias caveat).

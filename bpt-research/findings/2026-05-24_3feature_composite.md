# Adding trade_flow_ewma as a third feature — orthogonal but too weak

**Date:** 2026-05-24
**Author:** follow-up to `2026-05-23_composite_weight_stability.md`
**Status:** Complete — nuanced result. Trade-flow is orthogonal to OFI/microprice as predicted but too weak in absolute terms to lift mean composite IC.

## Question

The stability diagnostic identified the residual 35.7% loss-to-best-single in the 2-feature per-instrument composite as **information starvation** (not data starvation). Hypothesis: adding a third feature with low correlation to both OFI and microprice would expand the information set and improve OOS IC on the loser cohort.

Trade-flow EWMA — signed taker volume, EWMA'd with a 1s halflife — was the first candidate. Mechanically distinct from OFI (resting-book imbalance) and microprice (snapshot bid/ask qty asymmetry); captures the *aggressing-side* bias from actual fills.

Does it help?

## Setup

| | |
|---|---|
| Data | Same 10 HL hour-00 canon days, 7-train / 3-test |
| Universe | 182 instruments (all with full coverage) |
| New feature | `trade_flow_ewma(bbo, trades, halflife_s=1.0)` — added to `ic/features.py` |
| Trade aggressor sign | side=BUY → +qty, side=SELL → −qty, NULL dropped |
| Per-instrument fit | OLS on z-scored features (per-cell z-score); compare 2-feature (OFI+MP) vs 3-feature (+trade_flow) composites |
| Script | `bpt-research/experiments/ofi_3feature_composite.py` |

## Pairwise correlation check (pooled across all train cells)

| Pair | Spearman |
|---|---|
| OFI vs microprice_dev | +0.145 |
| OFI vs trade_flow | **+0.060** |
| microprice_dev vs trade_flow | **+0.036** |

Trade-flow's correlations with both existing features are an order of magnitude lower than OFI/microprice's mutual correlation. **The orthogonality requirement is met.**

(Note: the +0.145 OFI/microprice number is the cell-pooled Spearman across all 182 instruments. Per-instrument correlations vary widely — up to +0.51 on SOL/ETH, as low as +0.13 on AAVE.)

## Result — orthogonal AND weak ≠ uplift

### Headline (OOS, 3 days × 182 instruments)

| Signal | Mean IC | Median IC | % Positive |
|---|---|---|---|
| OFI alone | +0.144 | +0.133 | 97.8% |
| Microprice alone | +0.097 | +0.088 | 96.7% |
| **Trade-flow alone** | **+0.012** | **+0.007** | **62.6%** |
| 2-feature composite | +0.157 | +0.146 | 99.5% |
| 3-feature composite | +0.157 | +0.147 | 99.5% |

**3-feat beats 2-feat on 86 / 182 instruments (47.3%) — coin-flip. Mean uplift: −0.0001 IC.**

The 3-feature composite essentially matches the 2-feature composite on mean IC. Median is a hair higher (+0.001), positive-cell rate unchanged.

### Where trade-flow *does* help (top uplift cases)

Full-precision weights show trade-flow getting meaningful weight where it carries signal:

| Symbol | w_OFI | w_MP | w_TF | 2-feat IC | 3-feat IC | Uplift |
|---|---|---|---|---|---|---|
| LIT  | 1.3e-5 | 4.7e-6 | **2.0e-5** | 0.179 | 0.195 | +0.016 |
| HYPE | 9.6e-6 | 1.4e-5 | **1.5e-5** | 0.162 | 0.170 | +0.008 |
| TON  | 6.9e-6 | 1.4e-5 | **2.4e-5** | 0.090 | 0.096 | +0.006 |
| VVV  | 3.8e-5 | 2.8e-5 | 2.7e-5 | 0.223 | 0.227 | +0.003 |
| LTC  | 5.0e-6 | 6.4e-6 | 2.9e-6 | 0.143 | 0.147 | +0.003 |

On LIT, OLS gave trade-flow a *larger* weight than OFI. On these instruments, the orthogonal information was real and the composite captured it.

### Where trade-flow hurts (bottom uplift)

| Symbol | w_OFI | w_MP | w_TF | 2-feat IC | 3-feat IC | Uplift |
|---|---|---|---|---|---|---|
| NEO | (sized) | (sized) | small | 0.145 | 0.139 | −0.006 |
| YGG | | | small | 0.275 | 0.269 | −0.006 |
| FOGO | | | small | 0.092 | 0.087 | −0.005 |
| ARB, PURR, CHILLGUY | similar pattern | | | | | −0.004 to −0.005 |

On these, trade-flow contributed near-zero useful signal; OLS still gave it a small weight that introduced test-time noise.

## Interpretation

1. **Orthogonality is necessary but not sufficient.** Trade-flow's pairwise correlations (0.036, 0.060) are exactly what we wanted — but its standalone IC (median +0.007, 37% of instruments negative) is too weak. Per the rule of thumb:
   ```
   useful feature = high IC with returns + low correlation with existing features
   ```
   Trade-flow has the second piece but fails the first on most instruments.
2. **The 17 instruments that benefited** (3-feat uplift > +0.003) are roughly the LIT/HYPE/TON cohort — instruments where the 2-feature composite was already mediocre and trade-flow happened to carry real signal. These are exactly the "information-starved" cases the stability diagnostic predicted would benefit from more features.
3. **The trade is net-zero on average.** Gains on the right cohort cancel losses on the wrong cohort. The fix isn't to drop the feature globally; it's to **selectively include it per-instrument** based on whether it adds train-set signal.
4. **A stronger orthogonal feature would change this.** If we found a feature with corr ~0.05 to OFI/MP *and* median IC ~0.1+, the uplift over 2-feature would be substantial. The weak link is the candidate, not the methodology.

## Implications

1. **The per-instrument 2-feature composite (yesterday's +0.157 result) remains the operating point.** No reason to switch to 3-feature globally — the average gain is zero.
2. **Per-instrument feature inclusion** could close some of the residual gap. For each instrument, fit train-set OLS with all candidate features, then include only those with statistically-significant slopes. Forward stepwise selection per instrument. Cheap follow-up if the 3-feature approach is to be revived.
3. **Alternative third features worth trying**:
   - **Microprice momentum** (∂microprice/∂t over a short window) — different time-derivative of book state vs OFI's level-derivative
   - **Spread innovation** (recent change in `ask − bid`) — captures market-maker confidence shifts; should be uncorrelated with all three current features
   - **Trade-flow at different halflives** (0.5s, 5s, 30s) — current 1s might be off
4. **The per-instrument feature scorecard is the right artifact going forward.** Per instrument, which features carry signal at the test horizon? Build that table once and use it to inform both feature selection AND strategy routing decisions.

## Caveats

1. **Single halflife for trade-flow.** 1s was a guess. The IC numbers might shift meaningfully with halflife tuning. Would be the first thing to try before abandoning this feature.
2. **Liquidity-correlated weakness.** Trade-flow needs *trades* — instruments with sparse trade arrivals can't compute it reliably. Of the 182 instruments, the ones with median trade IC near zero are likely the lower-volume tail. Worth a stratified analysis.
3. **No nested CV.** Single 7/3 split. Standard caveat from earlier finds.

## Reproduce

```bash
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
  python bpt-research/experiments/ofi_3feature_composite.py
# output: /tmp/bpt_canon/composite_3feature.csv
```

## Next experiments (highest information value first)

1. **Trade-flow halflife sweep** — try {0.1, 0.5, 1, 5, 30, 300} seconds. If a different halflife has materially higher median IC, the 3-feature uplift story changes.
2. **Microprice momentum or spread-innovation as the 3rd feature** — different mechanism, potentially stronger standalone IC.
3. **Per-instrument feature selection** — forward stepwise, include each feature only on instruments where its train slope is statistically distinguishable from zero.
4. **Move to OFI-as-cancellation / sizing prototypes** (deferred from the parent thread) — signal calibration is mostly exhausted; the AS-consumption mechanic is the bigger remaining bottleneck.

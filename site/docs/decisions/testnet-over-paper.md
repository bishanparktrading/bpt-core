# Testnet over paper-mode

**Choice:** real testnet capital, no in-process synthetic-fill engine.

**Main alternative considered:** paper-mode (in-process fill simulator). **Tried,
then removed.**

## What I tried

A `PaperOrderGatewayClient` + `PaperFillEngine` behind `[fenrir.strategy].paper_mode`.
Front-of-queue fills at the limit price, zero fees, no post-fill mark-to-market.
Landed 2026-04-20 ([commits 3ff4bad + f5f6f87](https://github.com/bishanparktrading/bpt-core/commits/main)).

Exercised for ~45 hours on HL XMR alongside live HL testnet running the same
AS strategy with the same params.

## What broke the model

Side-by-side comparison in the same trending market window:

| Mode | Fills (15 min) | Spread captured | Balance |
|---|---|---|---|
| **Paper** (front-of-queue, no fees, no mark-to-market) | 4 fills | 3.4 bps | flat |
| **Live testnet** (real adverse-selection, real fees, real mark-to-market) | 23 fills (incl. 10 PARTIALS) | 8 bps widened | **−$36** on a 1.5%/5min downtrend |

The difference was textbook AS adverse-selection in a trending market — a thing
`PaperFillEngine` cannot model by construction.

I made tuning + market-selection decisions on the **paper signal** that the
**live signal later overturned**. That is the worst possible failure mode for
a backtest tool: it tells you what you want to hear, with high confidence, in
the wrong direction.

Removed 2026-04-22 ([commit 29185aa](https://github.com/bishanparktrading/bpt-core/commit/29185aa)).

## What I did instead

For venues without testnet (Hyperliquid mainnet-only, until they shipped testnet
in 2025): **small real capital or skip the venue.** Never propose reviving a
paper-mode-like simulator. Captured as a permanent rule in the engineering
notes:

> No synthetic fill simulators. paper-mode removed 2026-04-22; venues without
> testnet → small capital or skip. Never propose reviving.

## What about backtesting against historical data

That's a different problem and a separate tool — `bpt-backtester`. Backtest
replay against captured tape ([bpt-tape](../services/tape.md)) is at least
*replaying real fills that actually happened*. The market-microstructure simulator
problem is genuinely hard; serious shops invest years and many person-months in
their replay infrastructure. Pretending a 200-line `FillEngine` matches reality
is worse than not having one.

## What this taught me

The point isn't "paper trading is bad." Backtest replay is essential. The point
is: **a fill model that doesn't reflect adverse selection produces signal that
is not just inaccurate — it is anti-correlated** with what the live system would
do in the same conditions. Acting on it is worse than having no signal.

Better calibration heuristic: if a tool gives you a clean signal that looks too
good and you can't articulate exactly which mechanism would make it wrong in
prod, *that itself* is the thing that should make you suspicious.

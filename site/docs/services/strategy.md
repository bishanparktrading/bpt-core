# bpt-strategy

Strategy framework + the strategies themselves. One process per strategy variant
(role-qualified `comm` name like `bpt-strat-as`, `bpt-strat-rs`, `bpt-strat-ofi`)
so multiple strategies can run side-by-side under separate systemd units.

## Strategy roster

| Type | Notes |
|---|---|
| **Avellaneda-Stoikov** (`as`) | Classic MM with reservation price + spread. Extended with [Cartea-Jaimungal drift](https://github.com/bishanparktrading/bpt-core/blob/main/bpt-strategy/include/strategy/strategy/avellaneda_stoikov_strategy.h) + 3-layer suppression (drift / analytics / vol_gate) + queue-position awareness via L2 ladder + FairValueEstimator (mid / micro / L2 / EWMA). |
| **Regime-switch** (`rs`) | Hurst-based regime detector + variant selection. |
| **OFI** (`ofi`) | Order-flow imbalance. |
| **HMM** (`hmm`) | Hidden Markov regime detection. |
| **Momentum** (`mom`) | Trend-following. |
| **Funding-arb** (`farb`) | Cross-venue perp funding. |

## Framework pieces

- **Strategy base** — pure-virtual `IStrategy` with hooks: `on_orderbook`, `on_bbo`, `on_trade`, `on_exec_report`, `on_account_snapshot`.
- **MdClient / RefdataClient / VolSurfaceClient** — typed subscribers wrapping Aeron.
- **AeronOrderGatewayClient** — typed publisher for orders + subscriber for execs/snapshots.
- **PositionTracker** — strategy-side position view; reconciled against AccountSnapshots from the order gateway.
- **Reconciler** — pure-logic divergence check, separate test suite.
- **Warm-start** — per-instrument EWMA / RegimeDetector state persisted to JSON post-flatten, reloaded on restart with TTL.
- **FeeCache + FundingRateCache** — bounded caches, pruned on instrument delist via `RefdataClient::handle_delta_fragment`.

## Hexagonal seam

Bus boundary via `StrategyAeronBus::build()` in
`bpt-strategy/src/messaging/aeron_bus.cpp` — sole place that calls `<Aeron.h>`
from the application layer. Pragmatic: kept existing client classes concrete
(they already had `std::function` callbacks) instead of lifting to formal port
interfaces, since there's no current test-seam payoff.

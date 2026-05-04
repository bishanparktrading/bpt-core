# bpt-analytics

Off-hot-path metrics computed from the live MD + exec streams. Subscribes;
publishes `ToxicityScore` on stream 5001 which the strategy consumes as a
side-suppression signal.

## What it computes

- **Markouts** — post-fill price drift at 1s / 5s / 30s windows (per fill, per instrument)
- **Toxicity score** — flow-direction-weighted markout signal
- **Fill rate** — orders accepted / orders sent (per venue, rolling window)
- **TTF** — time-to-first-fill (latency of strategy intent → venue ack)

## Architecture

Three lifted concrete subscribers (`ExecReportSubscriber`, `MdBboSubscriber`)
fed by `AnalyticsAeronBus::build()`. `ToxicityPublisher` emits computed scores
back on the bus. Pure off-hot-path: no real-time strategy intervention, just
observational signals consumed by the strategy's gate logic.

## See also

- [bpt-strategy](strategy.md) — consumer of the toxicity stream
- [bpt-order-gateway](order-gateway.md) — exec report producer

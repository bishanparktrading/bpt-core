# bpt-md-gateway

The market-data hot path. Subscribes to venue WebSockets, decodes JSON / FIX into
normalised structs, validates, and publishes SBE-encoded ticks on Aeron.

## What lives here

- **Per-venue WS adapters** — Binance, OKX, Hyperliquid, Deribit. Each inherits a shared `RunLoop` (boost::beast WS) + `AdapterBase` (parse + publish thread topology).
- **SBE encoders** for `MdBbo`, `MdTrade`, `MdOrderBook`, `FundingRateUpdate`.
- **MdPublisher** — folds validate + drop-rate breaker + SBE encode + Aeron offer into one class. CRTP-friendly: the chain `decoder → MdPublisher` is fully vtable-free. One per adapter (publisher-thread-confined validator state).
- **MdValidator** (owned by MdPublisher) — per-instrument sanity checks (price > 0, qty > 0, no crossed book, deviation guards) gated on `max_price_deviation_pct`.
- **ValidationDropBreaker** (owned by MdPublisher) — per-adapter circuit breaker. If MdValidator drops > 30% of publishes over a 60s window, the publisher latches off until restart. Disabled by default; opt-in once thresholds are tuned per venue.
- **Hexagonal bus boundary** — `IMdControlSource`, `IAckPublisher`, `IFundingRatePublisher`, `IMdPublisher` ports; `AeronBus::build()` wires the prod concretes.

## Hot-path microopts

- Inline-storage `InlineVec` for orderbook levels — no malloc per book
- Cache-line isolated atomics on the publish path
- Branch hints + adaptive consumer spin in the WS read loop
- OKX BookState moved from `std::map` to sorted `std::vector` (better cache behaviour at depth 5–20)

## Per-exchange decode latencies (live measurement)

| Venue | Channel | p50 (ns) |
|---|---|---|
| Hyperliquid | l2Book | ~50 |
| OKX | books5 | ~80 |
| Binance | bookTicker | ~30 |
| Deribit | book.{instrument}.100ms | ~120 |

Reported via Prometheus per-adapter histograms; snapshot-and-reset every ~5s.

## Recent work

- Hexagonal bus + CRTP ([commit 65cd7b0](https://github.com/bishanparktrading/bpt-core/commit/65cd7b0))
- OKX BookState sorted-vector ([commit a4a63a2](https://github.com/bishanparktrading/bpt-core/commit/a4a63a2))
- Inline-storage MdOrderBook ([commit 2e7f654](https://github.com/bishanparktrading/bpt-core/commit/2e7f654))
- Cache-line isolation ([commit 728954d](https://github.com/bishanparktrading/bpt-core/commit/728954d))
- MdWsClient extracted from RunLoop ([commit f755a18](https://github.com/bishanparktrading/bpt-core/commit/f755a18))

## See also

- [Architecture overview](../architecture.md)
- [Hexagonal bus boundaries](../decisions/hexagonal-bus.md)
- [CRTP on the hot path](../decisions/crtp-hot-path.md)

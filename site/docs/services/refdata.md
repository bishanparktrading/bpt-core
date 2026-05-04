# bpt-refdata

Instrument catalog, fee schedules, and (historically) funding rates.

## What it owns

- **Instrument mapping** — canonical per-venue symbol ↔ instrument-id ↔ base/quote ↔ tick/lot/contract size
- **Fee schedules** — maker/taker per tier, per venue
- **Refdata snapshots** + **deltas** on Aeron — every other service consumes these

## What it doesn't own (anymore)

Funding rates moved to `bpt-md-gateway` on stream 1005 ([commit history](https://github.com/bishanparktrading/bpt-core/commits/main/messages)).
Rationale: funding ticks come over venue WebSockets, not REST — colocating with
the rest of MD made sense given they ride the same connection.

## Hexagonal seam (reference impl)

`bpt-refdata` was the first service to land the bus-boundary hexagonal pattern
(commit `64b...` 2026-04-28): 5 ports + `AeronBus::build()`. Subsequent
services followed this template — the off-hot-path simplicity made it the
right place to prove the pattern before generalising.

The 5 ports:

- `IRefdataControlSource` — inbound; strategy/admin requests
- `IRefdataDeltaSink` — outbound; per-instrument changes
- `IRefdataSnapshotSink` — outbound; full catalog dumps
- `IFeeScheduleSink` — outbound; fee table
- `IRefdataStatusSink` — outbound; "I'm alive" + ready signal

## Future scope

`RestClient` was promoted to virtual + injected via `shared_ptr` through the
adapter ctor — that's the seam `bpt-tape` will use to substitute a recording
subclass that tees REST response bodies to disk for backtest replay (not yet
wired; see [bpt-tape](tape.md)).

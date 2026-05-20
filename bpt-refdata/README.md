# bpt-refdata

Trading-host-side reference data service. Reads the **secmaster snapshot**
(`/opt/bpt/data/instrument_mapping.json`, produced upstream by
[`bpt-secmaster`](../bpt-secmaster/)) and polls per-venue REST APIs for fee
schedules. Normalises into a canonical `Instrument` model; publishes
snapshots + deltas + fee schedules over Aeron for the rest of the trading
stack (strategy, ogw, mdgw, pricer, radar).

bpt-refdata is **not** the source of truth for instruments — that's
secmaster (AWS RDS, see [`SECMASTER.d2`](../SECMASTER.d2) at the repo root).
bpt-refdata is the Aeron-side cache + SBE encoder; consumers talk to it via
Aeron streams, never reach back to RDS or the snapshot file directly.

Fee schedules are still pulled directly from venue REST here. Future work:
move into secmaster's schema and delete the per-venue fee adapters
(would shrink this service to a thin "JSON → SBE → Aeron" shim).

See [service-anatomy.md](../docs/service-anatomy.md) for the canonical service shape.

## At a glance

![bpt-refdata at-a-glance](diagrams/at-a-glance.svg)

Source: [`diagrams/at-a-glance.d2`](diagrams/at-a-glance.d2). Rebuild with
`./diagrams/render.sh at-a-glance`.

## Streams produced

| Stream | ID | Contents | Cadence |
|---|---|---|---|
| `refdata_snapshot` | 1001 | `RefDataSnapshot` — scoped to the subscriber's filters (see below); full registry if no filters | on-demand (one snapshot per `RefDataSubscriptionRequest`) |
| `refdata_delta` | 1002 | `RefDataDelta` (per-instrument adds / status changes / heartbeat) | per-event + Hz heartbeat |
| `fee_schedule` | 1004 | `FeeSchedule` (per-instrument maker/taker bps) | one-shot at boot + on refresh |
| `refdata_status` | 1006 | `RefDataReady` (all enabled exchanges loaded), `RefDataError` | once at boot + per error |

## Streams consumed

| Stream | ID | Contents |
|---|---|---|
| `refdata_control` | 1003 | `RefDataSubscriptionRequest` from subscribers (asks for snapshot push, with optional filters) |

### Snapshot filtering

`RefDataSubscriptionRequest` carries two filter groups:

- **`canonicalFilter`** (preferred) — match by base/quote/instrument_type
  + optional exchange. Venue-agnostic; one filter can match the same
  instrument on multiple venues. e.g. `{base: "BTC", quote: "USDT",
  type: PERPETUAL, exchange: ""}` matches BTC/USDT perp on every venue.
- **`instruments`** (legacy) — match by venue-native `(symbol, exchange)`
  tuple. Tighter but venue-specific.

Resolution order in `refdata_snapshot_publisher.cpp::matches()`:

1. If `canonical_filters` non-empty → match against canonical filters only.
2. Else if `instruments` non-empty → match against legacy filters.
3. Else (both empty) → match all; return the full registry.

**All 8 strategies pass `canonicalFilter`s today** (see `bpt-strategy/src/strategy/*.cpp`). The "full registry" path is reserved for research/admin tools that explicitly want everything (pgweb-equivalent uses, dashboard universe view).

## Layers (which this service has)

| Layer | Status | Notes |
|---|---|---|
| Composition root | yes | `src/main.cpp` |
| Service | yes | `app/refdata_service.{h,cpp}` |
| Bus | yes | `messaging/aeron_bus.{h,cpp}` — `RefdataBus` |
| Routing | yes | per-venue adapter handles its own venue's universe; `SubscriptionManager` (`messaging/subscription_manager.{h,cpp}`) routes `RefDataSubscriptionRequest` filters to the snapshot publisher |
| Adapter | yes | `adapter/<venue>/<venue>_refdata_adapter.{h,cpp}` — 4 venues |
| Wire | yes | `http/rest_client.{h,cpp}` (Boost.Beast over TLS, with retries — shared across venues) |
| External codec | yes | `adapter/<venue>/<venue>_refdata_decoder.{h,cpp}` (JSON → Instrument). OKX has an extra `okx_refdata_auth.{h,cpp}` for the OAuth dance |
| Pub/Sub (slow) | yes | 4 publishers + 1 subscriber, all api/aeron split |
| Pub (hot) | **no** | — |
| Internal codec | yes | `messaging/codecs/sbe_fee_schedule_codec.{h,cpp}`. RefDataSnapshot / RefDataDelta / RefDataReady are SBE-encoded inline in the publisher .cpp (they're snapshot-style, not high-cadence — no separate codec class) |
| Domain logic | yes | `registry/instrument_registry.{h,cpp}`, `resolver/instrument_resolver.{h,cpp}`, `mapping/` (canonical ID generation + venue ↔ canonical maps), `metrics/` |

## REST instead of WebSocket

Same adapter / wire / decoder shape as md-gateway, just:
- Wire is `rest_client.h` (one-shot HTTP GET with retries + TLS) instead of a streaming WS.
- Adapter polls on a schedule (initial fetch + periodic refresh) instead of running a forever-WS-receive loop.
- No subscription state — every poll fetches the full venue universe.

The `rest_client_lib` Bazel target is split out and re-used by `bpt-tape`
(which subclasses `RestClient` to tee response bodies to disk for the
refdata REST-capture feature).

## The control flow

`bpt-refdata` is unusual in that it has an inbound control stream
(`refdata_control`) — but the control isn't "subscribe to instruments"
(which md-gateway has). It's "send me a snapshot with this filter":

```cpp
struct RefdataRequest {
    uint64_t correlation_id;
    std::vector<InstrumentFilter> instruments;    // by symbol + exchange
    std::vector<CanonicalFilter>  canonical_filters;  // by base/quote/type
};
```

The service has the full universe locally; the request tells it which
slice to push on the snapshot stream.

## Test seams

- Unit: `tests/unit/` — registry, resolver, mapping, fee codec round-trip.
- Component: `tests/component/` — venue decoder fixtures (captured JSON → expected Instrument records).
- `tests/test_refdata_service_seam.cpp` — drives `RefdataService` event handlers directly with `FakeRefdataControlSource` / `FakeRefdataSnapshotSink` / etc. All Fakes inherit the corresponding `api::*` ports.

## Reading order

1. `src/main.cpp`
2. `app/refdata_service.{h,cpp}` — main loop, adapter fan-in, control-stream handling.
3. `messaging/aeron_bus.{h,cpp}` — `RefdataBus` shape (4 pubs + 1 sub).
4. `adapter/binance/binance_refdata_adapter.{h,cpp}` — concrete venue example.
5. `registry/instrument_registry.{h,cpp}` — canonical universe + ID generation.
6. `resolver/instrument_resolver.{h,cpp}` — venue ↔ canonical lookup.

## Build + test

```bash
bazel build //bpt-refdata:bpt-refdata
bazel test //bpt-refdata:refdata_tests
```

# bpt-pms

Position management service. Polls one or more venues' balance + position
endpoints, normalises into a multi-venue `BalanceSnapshot`, publishes on
Aeron. Smaller cousin of bpt-refdata — same external-REST shape, narrower
surface.

See [service-anatomy.md](../docs/service-anatomy.md) for the canonical service shape.

## At a glance

```
        EXCHANGES (REST)                        INTERNAL CONSUMERS

   ┌──────────────┐                             ┌──────────────────┐
   │ Hyperliquid  │←── POST /info               │ strategy         │
   │   (info API) │     (clearinghouse state)   │ (cross-venue     │
   └──────────────┘                             │  position view)  │
          ↑                                     │ console          │
          │                                     └──────────────────┘
          ↓                                              ↑
   ┌────────────────────────────────────────────────────────┐
   │                       bpt-pms                           │
   │                                                        │
   │  HyperliquidBalanceAdapter                              │
   │    BeastHttpsClient (signed POST)                       │
   │    JSON parser → BalanceSnapshot (positions + ccys)     │
   │                            ↓                           │
   │                  PmsService (polls on cadence)         │
   │                            ↓                           │
   │  snapshot_pub  ──→ BalanceSnapshot (SBE) on stream 8001 │
   └────────────────────────────────────────────────────────┘
```

## Streams produced

| Stream | ID | Contents | Cadence |
|---|---|---|---|
| `balance_snapshot` | 8001 | `BalanceSnapshot` (positions + per-currency balances, multi-venue) | per poll interval (~5-30s) |

## Streams consumed

None — pms is pure outbound.

## Layers (which this service has)

| Layer | Status | Notes |
|---|---|---|
| Composition root | yes | `src/main.cpp` |
| Service | yes | `app/pms_service.{h,cpp}` |
| Bus | yes | `messaging/aeron_bus.{h,cpp}` — `PmsBus` (one field: `snapshot_pub`) |
| Routing | **no** | — |
| Adapter | yes | `adapter/hyperliquid_balance_adapter.{h,cpp}` (HL is the only adapter today) |
| Wire | yes | inline `BeastHttpsClient` usage inside the adapter (no shared wire class — small surface) |
| External codec | yes | venue JSON → `BalanceSnapshot` inline in the adapter |
| Pub/Sub (slow) | yes | 1 publisher (api/aeron split) |
| Pub (hot) | **no** | — |
| Internal codec | yes | `messaging/codecs/sbe_balance_snapshot_codec.{h,cpp}` |
| Domain logic | minimal | `adapter/balance_row.h` — domain types for the snapshot |

## Why pms is small

Compared to bpt-refdata which has 4 venue adapters and a registry, pms has
one (HL). Other venues' balances come via bpt-order-gateway's
`AccountSnapshot` stream (which is venue-by-venue, separate stream
identity per venue). pms exists for the cross-venue aggregation case + as
the dedicated subscriber for HL's `clearinghouseState` info-API endpoint
(which the order-gateway doesn't poll).

The architecture is positioned to grow more adapters if you wanted a
unified balance/position view across all venues from one place — but
today, only HL.

## Auxiliary binary: `balance_peek`

`tools/balance_peek.cpp` is a dev binary that subscribes to the
`balance_snapshot` stream and prints each row. Used to verify pms is
publishing without needing a strategy or the console wired up.

```bash
bazel build //bpt-pms:balance_peek
./bazel-bin/bpt-pms/balance_peek
```

## Reading order

1. `src/main.cpp`
2. `app/pms_service.{h,cpp}` — poll loop + publish on cadence.
3. `messaging/aeron_bus.{h,cpp}` — tiny `PmsBus` struct.
4. `adapter/hyperliquid_balance_adapter.{h,cpp}` — REST poll + JSON parse.
5. `adapter/balance_row.h` — output struct shape.

## Build + test

```bash
bazel build //bpt-pms:bpt-pms
```

No unit tests yet — covered indirectly by the balance_peek smoke check.

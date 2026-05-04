# bpt-order-gateway

The risk perimeter. Every order placed by `bpt-strategy` passes through this
process before reaching a venue. Every exec report from a venue passes through
on the way back. Compromise this service → compromise the trading capital. It
is built accordingly.

## What it does

1. **Subscribes** to the strategy → ogw control stream (`NewOrder`, `Cancel`,
   `CancelAll`, `Modify`, `AccountSnapshotRequest`).
2. **Runs the risk gate** on every `NewOrder` (six checks, see below).
3. **Routes** the order to the right per-venue adapter (OKX, HL, Binance, Deribit).
4. **Receives** WS exec reports + REST acks from the venue.
5. **Parses + normalises** them into SBE `ExecReport` frames.
6. **Publishes** exec reports + account snapshots back on dedicated streams.
7. **Tracks** real-time PnL via `PnlTracker` (per-instrument VWAP-entry).
8. **Latches** trading off if any of three breakers trip (daily-loss, reject-rate,
   disconnect-rate).

## Risk module

Lives in-process inside the order gateway (architectural decision: Option C —
single-gateway, single-strategy → no need for a separate `bpt-risk` service yet;
upgrade path documented for when multi-gateway or multi-strategy lands).

Six pre-trade gates run on every `NewOrder`:

| Gate | Fails on | Source of truth |
|---|---|---|
| `max_order_size_usd` | order notional > limit | TOML `[order-gateway.risk]` |
| `max_notional_per_order_usd` | qty × price > limit | TOML |
| `max_open_orders_per_venue` | open count exceeds limit | OrderStateManager |
| `max_orders_per_second` | rolling-window rate exceeds limit | RateLimiter |
| `duplicate_order_id` | client-side OID already in flight | OrderStateManager |
| `max_position_usd` | post-fill net qty × mark > limit | PnlTracker.net_qty_e8() |

Plus three post-trade latches that trip the global `set_trading_enabled(false)`
gate:

- **Daily-loss latch** (`PnlTracker`) — VWAP-entry realised P&L per instrument
  rolled to UTC midnight. Threshold breach latches; restart required to clear
  (deliberate: forces human review).
- **Reject-rate breaker** (`RejectRateBreaker`) — global ratio of REJECTED
  exec reports over a rolling window. Halts ALL trading on trip.
- **Disconnect-rate breaker** (`DisconnectRateBreaker`) — per-adapter; halts
  only that venue if its `connect_and_run()` exception count over the window
  exceeds the threshold.

All three latch. Restart required. No auto-clear on midnight, no cooldown.

## Order-idempotency story

An order placed but not acknowledged is the worst possible state — it might
already be live in the venue book. Each adapter handles this differently:

| Adapter | clOrdID strategy | Risk on reconnect |
|---|---|---|
| **OKX** | `cloid = session_prefix + "G" + orderId` (deterministic per strategy intent) | Safe — REST POST has no retry layer; failure rejects immediately |
| **Binance** | `cloid = "G" + orderId` | Safe — same |
| **Deribit** | queued + drained on login | **WAS unsafe** — fixed in [761a1e2](https://github.com/bishanparktrading/bpt-core/commit/761a1e2): drain now front-consumes per-frame so a mid-loop throw doesn't replay already-sent frames |
| **Hyperliquid** | nonce-based, no clOrdID | Safe from double-fill; **separate phantom-fill risk** handled by `HyperliquidReconciler` (see below) |

### Hyperliquid phantom-fill recovery

HL doesn't expose client order IDs. A WS drop / 5s timeout / "not connected"
mid-`post_action` leaves the strategy unsure whether the order landed. The
adapter pushes a `Candidate` to `HyperliquidReconciler` instead of immediately
emitting REJECTED. After a 3-second wait, the worker polls `/info openOrders`
+ `/info userFills` and matches on `(coin, side, qty_e8, price±1 USD,
time_window 1s)`:

- Exactly one open order → emit ACKED
- Exactly one userFill → emit FILLED (takes priority — more specific)
- No match → emit REJECTED
- Multiple matches → emit REJECTED + ERROR (relies on strategy reconciler to flag divergence)

17 unit tests cover the matching logic + worker-thread races. Required before
running AS on HL mainnet. Landed in
[41727a6](https://github.com/bishanparktrading/bpt-core/commit/41727a6).

## Position reconciliation

Every `AccountSnapshot` from the venue is diffed against `PnlTracker`'s in-process
view. Divergence > 0.0001 base units logs WARN + increments
`strategy_reconciliation_divergences_total`. Pure-logic `reconcile()` function,
7 unit tests; SPOT-aware (the reconciler rewrites the exchange-view map for
SPOT entries using `currencyBalances` rather than `positions`, since SPOT
doesn't expose positions in the usual sense).

## Credentials

Per-adapter via systemd-creds (encrypted blobs decrypted into
`$CREDENTIALS_DIRECTORY` at unit start). Strict mode in qa/prod: empty
`secret_name` for an enabled adapter throws at startup rather than running
silently with no auth.

`PR_SET_DUMPABLE=0` set as the very first thing in `main()` — protects HL
private keys from landing in core dumps if the process ever crashes.

## Observability

- `risk_pretrade_rejections_total` (per-reason counter)
- `risk_trading_enabled` (0/1 latch gauge)
- `risk_breaker_tripped` (per-breaker)
- `daily_pnl_usd` (per-instrument gauge)
- `orders_in_flight` (per-adapter gauge)
- `exec_report_latency_ns` (histogram)

Wired into Alertmanager: `DailyLossLatch` + `RejectRateBreaker` +
`DisconnectRateBreaker` are critical-severity (PagerDuty repage).

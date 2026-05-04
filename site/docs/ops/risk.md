# Risk

In-process inside `bpt-order-gateway`. The full design is on the
[order gateway page](../services/order-gateway.md#risk-module); this page is
the operator-facing summary.

## Gates that run on every NewOrder (pre-trade)

| Gate | Check | Action on fail |
|---|---|---|
| `max_order_size_usd` | order notional limit | reject + WARN |
| `max_notional_per_order_usd` | qty × price ceiling | reject |
| `max_open_orders_per_venue` | count limit | reject |
| `max_orders_per_second` | rolling-window rate | reject |
| `duplicate_order_id` | OID already in flight | reject |
| `max_position_usd` | post-fill net qty × mark | reject |

Every gate increments a per-reason Prometheus counter. Rejection rate by
reason is on the operator dashboard.

## Latches that fire on post-trade signal

| Latch | Trigger | Effect |
|---|---|---|
| Daily-loss | PnlTracker realised P&L breaches `max_daily_loss_usd` | flips global `set_trading_enabled(false)` — halts ALL trading |
| Reject-rate breaker | exec-report REJECTED ratio > threshold over rolling window | halts ALL trading |
| Disconnect-rate breaker | per-adapter `connect_and_run()` exception count > threshold | halts THAT venue only |

All three latch. **No auto-clear.** Restart required after human review.
This is deliberate — the failure mode that latches you out probably needs
human attention before resuming.

## Validation drop breaker (separate from order-gateway breakers)

Lives in `bpt-md-gateway`'s `ValidatingPublisher`. If MdValidator drops > 30%
of publishes for an adapter over a 60s rolling window:

- Stops forwarding to Aeron for that adapter (downstream sees no data, not bad data)
- Latches; restart to clear
- Disabled by default per-adapter; opt-in once thresholds tuned per venue

## On-call runbook (one-liner per scenario)

| Page | First action |
|---|---|
| `DailyLossLatch` | DON'T immediately restart — review fills + P&L; understand why before clearing |
| `RejectRateBreaker` | Check venue status page; could be venue-side or strategy-side |
| `DisconnectRateBreaker` | One venue down; other adapters keep trading; investigate WS issue |
| `ValidationDropBreaker` | Schema drift on a venue; freeze that adapter's strategy positions; investigate parser |
| `ReconciliationDivergence` | Cross-check exchange UI vs strategy view; figure out who's wrong |
| `MDGatewayQuiet` | Check WS connections; could be venue silence (legitimate quiet market) or our bug |

The "force a human" property of latching is the point. Trading is forgiving
of slow recovery; it is not forgiving of fast wrong recovery.

# Backlog

Known issues and follow-ups that are not urgent enough to block current work
but should be picked up when the related subsystem is next touched.

## Hyperliquid adapter

### WS reconnect reconciliation gap

**Status:** open, observed on HL testnet 2026-04-16.

**Symptom:** HL gracefully closes the `/ws` connection every ~10 minutes
(`"The WebSocket stream was gracefully closed at both endpoints"`). bpt-order-gateway
reconnects within ~2 s, but any `userFills` events generated on the exchange
during the reconnect window are lost — HL does not replay them after the new
subscription. Fenrir's inventory then diverges from the exchange by however
many fills happened in the gap.

**Measured impact (Apr 16 session, pre partial-fill fix):** ~24 fills leaked
across 4 reconnects over ~45 min of trading, causing fenrir to underreport
inventory by ~0.024 BTC vs the real account.

**Why it's currently tolerable:** with the partial-fill + cancel-race fixes
committed today, the observable divergence in steady-state (no reconnects) is
≤ 1 fill (just the natural race between fenrir's log line and an async poll
of `/info`). The reconnect gap is the dominant remaining source of drift.

**Fix options, roughly in order of increasing scope:**

1. **Process `isSnapshot=true` fills on subscribe, dedupe by `oid`.** HL sends
   a recent-fills snapshot on subscribe. If we dedupe by the `(oid, fill_time)`
   pair against an "already seen" set, we can replay missed fills without
   double-counting the ones we already processed. Minimal new state. Simplest
   path forward.
2. **Periodic reconciliation against `clearinghouseState`.** Every ~30 s (or
   on reconnect), fetch `/info { type: "clearinghouseState" }` and compare the
   reported position vs fenrir's internal tracker. If they differ, emit a
   synthetic correction event. Wraps the WS stream with a ground-truth
   safety net. More code but catches any drift mechanism, not just WS gaps.
3. **Keep the WS alive.** HL's ~10-min grace-close cadence may be avoidable by
   a different ping pattern or reconnection strategy — worth checking once the
   account clears the scheduleCancel volume gate and we can use HL's official
   keepalive hooks. Orthogonal to 1 + 2.

Recommended first step: option (1). It's a single-file change in the parser
and directly addresses the observed leak.

**Relevant code:**
- `bpt-order-gateway/src/adapter/hyperliquid/hyperliquid_ws_client.cpp` — reconnect loop
- `bpt-order-gateway/src/adapter/hyperliquid/hyperliquid_exec_parser.cpp` — `handle_fills`
- Log tag to watch: `"gracefully closed at both endpoints"` in `bpt-order-gateway.log`

## bpt-tape

Surfaced 2026-05-10 by the disk-full incident below. Read together: the
recorder is functional but not production-ready, and the failure modes are
silent in ways that make us notice 36+ hours late.

### Disk-full incident, 2026-05-09 00:22 UTC → 2026-05-10 05:34 UTC

**Symptom:** `mkdir("/opt/bpt/data/raw/hyperliquid/2026-05-09")` returned
`ENOSPC` and the writer thread silently looped on retry. Process stayed
"healthy" (WS connected, BBO decoder running, journal streaming) but no wslog
file was open. ~29 hours of HL tape lost. Detected by accident from a stale
local mtime in chat.

**Root cause:** the data EBS (`/dev/nvme1n1`, 30 GB) hit 100% used. May 8
captured 19 GB in 16 hours after a build was deployed at 08:21 UTC — the
post-deploy data rate is ~6× the pre-deploy rate (~3 GB/day before, ~19 GB/day
after). Sync to S3 was working fine but uses `rclone copy`, never deletes, so
old days accumulated until the volume filled.

**Recovery:** verified May 4–8 wslogs were byte-exact on S3, deleted them
locally, the writer's retry loop succeeded on the next mkdir, recording resumed.

### Silent failure: rotation/open errors are swallowed

`bpt-tape` writer retries on `mkdir`/`open` failure with no log line, no
metric, no crash. ENOSPC, EACCES, and EROFS all look identical from outside
the binary: process alive, decoder running, no wslog file. Fix: treat
rotation-open failure as `log_fatal` + abort. With `Restart=always` the
journal records the failure and any monitoring picks it up. Pre-create the
next day's directory at HH:00 (well before the HH:22 rotation) so the failure
mode shifts left and is visible while the previous file is still open.

### Missing local-disk lifecycle

`scripts/sync_tape_to_s3.sh` is `rclone copy` (intentionally not `move`, see
the script comment). Nothing else deletes local files. A separate cleanup
step is needed: after each successful sync, remove local wslogs older than N
days that have a matching `(size, mtime)` on S3. Without this, ENOSPC is
inevitable on any host that records longer than `disk_size / daily_rate`.

### Heartbeat unit not installed on prod

`deploy/env/prod-recorder.env.example` references
`bpt-recording-heartbeat.timer (5-min freshness check → Healthchecks.io)`.
The unit ships in the deploy bundle but was never installed on the Tokyo
recorder host. Without it nothing pages when capture stops. Install it, point
it at a Healthchecks.io check, page on miss.

### No metrics, no dashboard

The bpt-tape config already declares `[metrics] host=127.0.0.1 port=9110` but
nothing scrapes it and nothing visualizes it. Counters needed (some may exist
already, others to add):

- `bpt_tape_last_wslog_write_unix_seconds` (gauge) — single most important;
  alone would have caught this incident
- `bpt_tape_messages_decoded_total{stream}` and `..._persisted_total{stream}`
  — divergence between these is the silent-writer signal
- `bpt_tape_wslog_rotations_total` and `..._rotation_failures_total`
- `bpt_tape_ws_reconnects_total{stream}`
- `bpt_tape_open_fds`
- `bpt_tape_disk_free_bytes{mount="/opt/bpt/data"}`

Grafana dashboard with one row per category, alert rules on
`time_since_last_write > 5min`, `decoded - persisted > 5min`,
`disk_free < 20%`, `ws_reconnects[5m] > 3`.

### Logs only on the host

`/opt/bpt/bpt-tape/logs/bpt-tape.log` rotates by size (~10 MB), keeps a
handful of files, then drops. Anything older than the rotation window can't
be queried after the fact. Ship `journalctl -u bpt-tape` to a central store
(Loki + Promtail is cheapest, CloudWatch Logs is laziest). Keep ≥30 days.

### Terraform drift: prod doesn't match the modules

The repo has `infra/terraform/tape-host`, `tape-storage`, `tape-iam`. They
describe an instance with a 500 GB `/opt/bpt/data` EBS, an Elastic IP,
least-privilege `bpt-tape-archiver` (writer) and `bpt-tape-reader` (reader)
IAM users, and a 3-tier S3 lifecycle. Reality on 2026-05-10:

- `/opt/bpt/data` is **30 GB**, not 500 GB
- Hostname is the AWS-default `ip-10-42-1-221`, not a meaningful DNS name
- Operator's laptop rclone uses **`admin` (root) AWS keys**, not
  `bpt-tape-reader`
- S3 lifecycle policy on the live `bpt-tape-archive` bucket — unverified

Pick a forcing function: either run `terraform plan` against current state
and reconcile, or rebuild the host from the modules into a new instance and
cut over. Either way, drift this large means the IaC isn't a source of
truth.

### Hostname / operability

Even with terraform reconciled, `ip-10-42-1-221` should become
`tape-hl-tokyo-01.bpt.internal` (or similar) — `hostnamectl set-hostname` +
EC2 Name tag (already set to `bpt-tape`, partial credit) + Role/Env/Venue
tags + a Route53 internal record so we ssh by name, not IP. Without DNS,
losing the IP from session shell history (which has happened — the Singapore
IP we tried earlier is now somebody else's box) means scrambling.

### systemd --user instead of system service

Current unit lives in `/home/ubuntu/.config/systemd/user/bpt-tape.service`,
parented by `systemd --user` (PID 11701). It works but binds the recorder's
lifecycle to a user session, depends on `loginctl enable-linger`, and the
deploy bundle's `bpt-recording.target` doesn't apply. Move to a system unit
(`/etc/systemd/system/bpt-tape.service`) under a dedicated `bpt` system
user, matching the prod-recorder.env.example assumption.

### Capacity planning open question

May 4–7: ~3 GB/day. May 8 (post-deploy): ~19 GB/day — files grew from
~150 MB/hour to ~2.1 GB/hour. Either the universe expanded, depth changed,
the refdata REST capture started writing meaningfully, or a new field is now
being recorded. Steady-state rate determines volume sizing, S3 cost
projections, and alert thresholds. Diff the two builds' recording paths and
config to identify which.

**Relevant code / paths:**
- `bpt-tape/src/io/tape.cpp` — Tape open/rotate/write paths
- `scripts/sync_tape_to_s3.sh` — sync script (uses `rclone copy`)
- `deploy/env/prod-recorder.env.example` — references uninstalled heartbeat
- `infra/terraform/tape-{host,storage,iam}/` — design that prod has drifted from
- Ops fact: recorder is `i-0f245857ada6a3312` in `ap-northeast-1`,
  EIP `13.193.107.7`, ssh `ubuntu@13.193.107.7` with `~/.ssh/bpt_tape_ed25519`

### MdValidator interprets `max_price_deviation_pct = 0` as "fail every nonzero deviation"

The bpt-tape config sets `max_price_deviation_pct = 0.0` and the inline
comment says `disable validator on the recorder`. The implementation
treats it as a strict threshold instead — at 0% allowed deviation, every
nonzero tick produces a `[W] [MdValidator] BBO price deviation X% > 0.00%`
warning. On the 230-perp HL recorder this generates **hundreds of
warnings per second** in the journal.

Two ways to fix in `bpt-md-gateway/.../md_validator.{h,cpp}`:

1. Sentinel: treat `max_price_deviation_pct <= 0` as "validation disabled"
   and skip the comparison entirely. Matches the operator's intent and
   keeps the existing config working unchanged.
2. New flag `validation_enabled = false` + leave threshold semantics as
   "strict". More explicit but breaks the "<=0 means disabled" inference
   most readers will form.

(1) is recommended. Until either lands, the recorder bleeds CPU on Quill
serialization for warnings nobody will read, and the journal eats disk
every night when systemd rotates.

**Relevant code:**
- `bpt-md-gateway/include/md_gateway/validation/md_validator.h`
- `bpt-tape/config/bpt-tape.hl.toml:42` — config call site

### bpt-tape has zero unit tests

`find bpt-tape -path "*test*"` returns nothing. The recorder is now a
production service responsible for capturing data we can't recover —
the absence of tests is a P0 deficiency now that we're treating the
output as authoritative.

Three tests would cover the highest-risk paths:

- `test_loader.cpp` — TOML round-trip, missing-required-field errors,
  recording_universe_venues filter. Catches "config silently
  misinterpreted" regressions which are the worst class of recorder bug.
- `test_refdata_poller.cpp` — schedule logic (next_due math), error
  recovery (one bad endpoint doesn't take down siblings), stop()
  responsiveness while a poll is in-flight.
- `test_tape_rotation.cpp` — date-rollover at UTC midnight (the
  exact failure mode from 2026-05-09); ENOSPC simulation via a fake fopen
  that returns `nullptr` once, asserting the new metrics hook fires and
  the bool return is false; also death-test the abort path in the
  recording adapter.

Add `cc_test` target to `bpt-tape/BUILD` mirroring the bpt-common one.

### Missing operational metrics: disk-free + WS reconnect counter

The grafanalib dashboard at `monitoring/dashboards/bpt_tape.dashboard.py`
references `node_filesystem_avail_bytes{mountpoint="/opt/bpt/data"}`,
but the recorder host doesn't run prometheus-node-exporter — those
panels stay blank. Two cheap fixes:

1. Self-emit `bpt_tape_disk_free_bytes{mount="/opt/bpt/data"}` from
   TapeMetrics — one `statvfs()` call every 30s, gauge updated in a
   small helper thread. No external dep, no SG change.
2. Expose `bpt_tape_ws_reconnects_total{venue}`. The data already lives
   in `ConnState::reconnect_count` inside the on_connect/on_disconnect
   closures — surface it via the metrics struct so connection flapping
   is visible on the dashboard rather than buried in journal lines.

Both fit in TapeMetrics with no API change to Tape. Adding a
`DiskUsageRollupAlertRule` would close out the disk side of the alert
matrix once the gauge exists.

### wslog file format has no version stamp

`Tape`'s `SESSION_START` marker writes a JSON payload `{"pid":N,"exchange":...,"ws":...}`
but no format-version field. If the binary record header
(`uint64 ts | uint8 type | uint32 length | payload`) ever changes —
e.g., adding a venue tag, switching to varint length — old wslogs
become unreadable with no programmatic way to detect the schism.

Add `"format_version": 1` to the SESSION_START payload. Then bump on
any record-format change. The replay-side parser checks the version on
file open and refuses to decode formats it doesn't understand, instead
of silently producing garbage. ~15 min of code, ~30 years of forward
compatibility for the archive we're now treating as authoritative.

**Relevant code:**
- `bpt-tape/src/main.cpp` — SESSION_START payload construction
- `bpt-common/include/bpt_common/recorder/wslog_format.h` — RecordType enum
  (also: the unused `CHECKPOINT` value here — write it or remove it)

### bpt-tape cosmetic cleanups (small, batch as one PR)

Low-priority but worth picking up next time the file is open:

- `bpt-tape/src/main.cpp:12` — header comment claims refdata REST
  capture is "a planned extension" but it's been shipped (commit
  `02e7ab7`). Drop the future-tense paragraph.
- Two ad-hoc string-case helpers (`lowercase_venue` in main.cpp,
  `to_upper` in refdata_poller.cpp) — same shape, different files.
  Pull into `bpt-common/util/strings.h`.
- `RecordType::CHECKPOINT` enum value (`tape.h:46`) has
  no producer in the codebase. Either implement the periodic-heartbeat
  write the comment promises, or remove the enum value so readers
  don't think they're missing something.

## bpt-canon (derived event tape)

Shipped 2026-05-20 with wslog→canon parity + OKX trades/L2 ingesters + merger
+ backtester `--canon` reader. Three known gaps documented during the OKX
sanity-check run:

### OKX L2 ingester accumulates stale levels

**Status:** open, observed on the OKX BTC-USDT-SWAP 2026-04-30 day.

**Symptom:** End-of-run `[AS] Book #6030000` log line shows
`ladder bids=13347 asks=13326 best_bid=76620.9 best_ask=75281.5` — bid above
ask. Real OKX top-of-book is 400 levels per side; the ingester maintains
the full historical price set in `std::map<double, double>` and never
trims. Over 24h the tail accumulates 30× more stale levels than ever
existed live.

**Why it didn't break the sanity check:** top-of-book is still correct
because OKX always re-emits the live top-400. The crossed-book at the very
end is a transient where a fast-moving price left an old best-bid behind
without OKX explicitly removing it (the level fell off the top-400 stream
and the ingester has no way to know that — only `size=0` removes a level).
The matching engine walks deque-front so the strategy still trades against
sensible top-of-book; queue-position estimates further back are wrong.

**Fix:** after each apply, cap each side to top-400 by price (drop
worst-priced entries beyond the cap). Or: trust the snapshot lines (which
fire every ~15 min in this archive) to fully replace the book.

**Relevant code:** `bpt-canon/src/ingest_okx_l2_main.cpp` — `BookState`
struct and `apply_level` methods.

### OKX fee schedule not loaded into backtester

**Status:** open, observed when first OKX canon backtest produced
`fee_paid=0` for every fill across 360 fills.

**Symptom:** `trades.csv` reports zero fees for all OKX fills. On
BTC-USDT-SWAP this hides ~5 bps taker cost / -2 bps maker rebate per fill,
which on 360 fills across $76k notional adds up to noticeable unmodeled
P&L. The HL backtest path produces correct PnL because the HL backtest
config wires fees through the venue-exec block; the equivalent isn't
loaded for OKX in the harness's refdata seed.

**Why:** `StrategyHarness::initialize()` populates
`refdata_client_->cache()` from `instrument_mapping.<venue>.json` but
never seeds `FeeCache`. The live refdata service loads
`config/exchanges/{mainnet,testnet}.toml` fee tables; harness doesn't
call that path.

**Fix:** wire `bpt-refdata`'s fee-schedule loader into the harness
initialize path. Load the venue fee tables, seed `refdata_client_`'s
fee cache. `ResultsCollector::apply_fill` already reads through that
cache — no consumer changes needed.

**Relevant code:**
- `bpt-backtester/src/harness/strategy_harness.cpp::initialize()` — loads
  InstrumentMappingLoader but not FeeScheduleLoader.
- `bpt-refdata/include/refdata/fee/...` — loader already exists.

### OKX tick/lot hardcoded in StrategyHarness

**Status:** open.

**Symptom:** `strategy_harness.cpp::initialize()` hard-codes
`if (e.venue_symbol == "BTC-USDT-SWAP") { inst.tick_size = 0.1; inst.lot_size = 0.01; }`
because `instrument_mapping.okx.json` doesn't carry tick/lot fields.
Every new OKX instrument I want to backtest needs a code edit + rebuild.

**Why it matters:** AS's post-touch quote cap is gated on `tick_size > 0`.
Instruments not in the hardcoded allowlist fall back to `tick_size = 0`
and quotes can cross the book, producing a quote-storm of immediate
fills/cancels (this was the HL APE bug fixed 2026-05-20).

**Fix:** persist `tickSz`/`lotSz` into `instrument_mapping.okx.json` so
the harness reads them like any other field. Either: (a) the
`bpt-universe` pipeline calls OKX `GET /api/v5/public/instruments` at
mapping-refresh time and stores the values; or (b) write a small one-off
script that does the same. (a) is cleaner because universe-refresh
already runs periodically.

**Relevant code:**
- `bpt-backtester/src/harness/strategy_harness.cpp::initialize()` — the
  hardcoded `if (venue.id == OKX)` block.
- `bpt-universe/...` — natural home for an OKX instrument-meta fetcher.

## Trading-floor tooling (HFT-trader desk parity)

These map onto what a real prop-shop trader has in front of them all day.
The existing stack (Grafana for ops, React dashboard for trading) already
covers most of it; this section is the gap.

Reference for what's already shipped — don't rebuild these:

- Grafana: service health, exchange connectivity, order/MD rates, order
  ACK RTT histograms (p50/p90/p99), tape recorder metrics
  (`monitoring/dashboards/bpt_system_overview.dashboard.py`,
  `bpt_tape.dashboard.py`)
- React `/`: live PnL, position, working orders
- React `/archive`: backtest archive view
- React `/radar`: vol smile, heatmap, calendar, funding-rate panels
- Alertmanager → Discord for critical/warning ops alerts

### MD wire→decode latency histogram

**Status:** open, partial.

**Symptom:** Grafana has order ACK RTT (the round-trip after we send an
order) but not the *inbound* latency from venue wire to strategy
callback. A 1 ms regression in BBO decode would not surface today until
it manifested as a missed-fill PnL drag, which is days too late.

**Fix:** wire a histogram metric from the HL/OKX decoder hot path
(`bpt-md-gateway/src/messaging/publishers/md_publisher.cpp` already
records latencies internally — just needs a `prometheus_cpp::Histogram`
exposed). Then add a Grafana panel mirroring the order-ACK RTT shape.

**Relevant code:**
- `bpt-md-gateway/include/md_gateway/md/md_publisher_concept.h` — Pub
  concept (would need a `record_latency` method, or a sibling metric
  surface).
- `bpt-md-gateway/src/messaging/publishers/md_publisher.cpp` — the actual
  publish hot path already times itself (TscClock::now_mono_ns deltas).

### MD decode→strategy fan-out latency

**Status:** open.

**Symptom:** no metric exists for "decoder finished, strategy on_bbo
returned." If the strategy spends 500µs on every BBO (vs the 20µs we
think it does), it's silently bleeding alpha and the only signal is fills
landing 480µs later than they should.

**Fix:** wrap the `client_.push_bbo(msg)` call in
`bpt-md-gateway/include/md_gateway/messaging/publishers/md_publisher.h`
(or equivalent strategy-side md-client receive path) with a TscClock
delta into a histogram. Per-handler granularity — `strategy_md_callback_ns`
keyed by message type.

### Live trade blotter (React dashboard)

**Status:** open, would be a new route or panel on `/`.

**Symptom:** trades.csv exists after a backtest, but during a live session
there's no chronological "every fill in order" view. Trader has to grep
logs to answer "what was that last fill?"

**Fix:** new `<TradesBlotter>` React component subscribing to
ExecReport events from the WS bridge. Filter/sort by venue, symbol, side,
strategy, time. ~1 day.

**Relevant code:**
- `bpt-bridge/src/app/bridge_service.cpp` — WS bridge that fans
  ExecReports out to the React frontend.
- `bpt-console/...` (or wherever the React dashboard lives) — sibling
  to the existing PnL panel.

### Cross-strategy risk aggregator

**Status:** open, becomes important the first time we run >1 strategy
concurrently.

**Symptom:** each strategy enforces its own `max_position_usd`,
`max_daily_loss_usd`, etc. But across multiple strategies on the same
underlying (or multiple underlyings highly correlated), the aggregate
exposure can be far above intent. Today: nothing shows total exposure
across strategies.

**Fix:** new panel on React `/` that subscribes to all strategies'
PositionUpdate events and shows total net notional by venue, by asset,
by side. Plus aggregate daily P&L. Plus a hard kill-switch
(`/kill` endpoint that signals all strategies to stop quoting).

**Relevant code:**
- `bpt-strategy/include/strategy/strategy/position_tracker.h` — per-strategy
  position. Need cross-strategy aggregation surface.
- `bpt-bridge/...` — would need to fan position updates to dashboard.

### TCA / markout dashboard (live)

**Status:** open, partial — bpt-analytics computes markouts already.

**Symptom:** `bpt-analytics` computes markouts at 50ms/1s/5s/30s after
fill and exposes them on stream 5001 (per `[[project_tyr_service]]`
memory). But there's no UI showing the rolling markout distribution per
strategy in real time. Today: read trades.csv after the run, plot in
notebook. Slow loop.

**Fix:** new `<TcaPanel>` React component on `/` or as new route
`/tca`. Subscribes to bpt-analytics's MarkoutReport stream. Rolling
window: per-strategy avg markout, p25/p50/p75, buy-vs-sell asymmetry.
Sparkline per symbol.

**Relevant code:**
- `bpt-analytics/include/analytics/messaging/publishers/toxicity_publisher.h`
  — already publishes the data.
- `bpt-bridge/...` — needs the MarkoutReport fan-out wiring.

### Strategy state machine visualization

**Status:** open.

**Symptom:** AS strategy can be running, paused (cooldown), halted (vol
gate), suppressed (drift, toxicity, queue ahead), or fully stopped
(daily-loss hit). Today: read log lines to figure out which. No glanceable
visual answer to "why isn't my strategy quoting?"

**Fix:** new panel on `/` showing per-strategy state machine + reason
code. Last 60s of state transitions with timestamps. Suppression
breakdown: which gate (drift / vol / tox / queue) is firing, current
value vs threshold.

**Relevant code:**
- `bpt-strategy/src/strategy/avellaneda_stoikov_strategy.cpp` — already
  computes all this state internally for log lines. Needs to publish as
  a structured event.

### L3 order book heatmap (Bookmap-style)

**Status:** open, nice-to-have.

**Symptom:** the React dashboard shows top-of-book and L2 ladder
snapshots but no time × price × resting-size heatmap. This is the
canonical "what does the market look like?" view at any HFT desk and
genuinely useful for spotting iceberg orders, spoofing patterns, and
post-trade book reactions.

**Fix:** new `<BookHeatmap>` component using canvas (not SVG — 1000s of
cells refresh per second). Subscribe to L2 updates, accumulate
`(timestamp_bucket, price_bucket) → size` matrix, render with viridis
color scale. Tick scroll horizontally; pause to inspect.

This is more work than the others (~1 week of frontend) and the
Bookmap-style viz is more of a "feel" thing than a strict need; defer
until other items above are done.

### PnL / risk alert routing

**Status:** open, partial — alertmanager exists, ops alerts wired up.

**Symptom:** Discord routing covers critical/warning ops alerts
(`bpt-alerts.yml`) but not strategy-PnL alerts. If AS hits its daily-loss
threshold and pauses, the operator finds out by reading logs, not by
phone vibrating.

**Fix:** new Prometheus alert rules group `bpt-trading` with rules like:
- `StrategyPauseDailyLoss` — strategy_trading_paused == 1 for >30s
- `StrategyPositionApproachingMax` — pos_pct_of_max > 0.8 for >60s
- `MarkoutsTurnedNegative` — avg_markout_5s_bps < -10 over 50 fills
- `FillRateCollapsed` — fill_rate over 1h falls below 1/3 trailing 7d

Routed to Discord with `trading` tag so operator can distinguish from
ops alerts.

**Relevant code:**
- `monitoring/rules/bpt-alerts.yml` — append a `bpt-trading` rules
  group.
- `monitoring/alertmanager/alertmanager.yml` — already has Discord
  receiver, just needs a route for `severity=trading`.

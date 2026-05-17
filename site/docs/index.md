# bpt-core

**Production-grade C++ HFT stack — 8 services, sub-microsecond hot paths, deployed on AWS.**

A self-built market-making + research stack for crypto perps and spot. Trades on
Hyperliquid, OKX, Binance, and Deribit. Designed around the question: *what would
a one-pod quant shop need to actually run live?*

---

## At a glance

<div class="grid cards" markdown>

- :material-speedometer: **~50 ns**
  ::: BBO decode latency (Hyperliquid, measured)

- :material-server-network: **8 services**
  ::: composing one trading stack via Aeron IPC

- :material-source-branch: **35 Bazel targets**
  ::: ~30k lines of C++20, ~5k lines of Java (Aeron MediaDriver wrapper)

- :material-currency-usd: **4 venues**
  ::: HL · OKX · Binance · Deribit (real testnet capital, not paper)

- :material-shield-check: **6 risk gates**
  ::: position cap, daily loss latch, reject-rate, disconnect-rate, validation-drop, order-size

- :material-chart-areaspline: **~10 GB/day**
  ::: tape capture per venue → S3 archive in Tokyo (ap-northeast-1)

</div>

---

## What this is — and isn't

**It is** a working stack:

- C++20 throughout the hot path. Aeron over shared memory. SBE wire format. Zero-malloc on the publish path.
- Real venue WS + REST adapters with TLS pinning, validation, circuit breakers, idempotent reconnect.
- A risk module enforced by the order gateway (position caps, daily-loss latch, reject-rate breaker, disconnect-rate breaker), with synthetic + organic test coverage.
- Observable: Prometheus + Alertmanager + Grafana, with PagerDuty-on-critical and Healthchecks.io as a dead-man's-switch.
- Deployed: systemd-managed services on a deploy.sh / rollback.sh tarball pipeline, validated against `/opt/bpt/`-style host layout.

**It isn't:**

- A toy or paper simulation. There's no synthetic-fill engine — venues without testnet either get small real capital or get skipped. Paper mode was [removed](decisions/testnet-over-paper.md) after the fills it modelled diverged from real adverse-selection in a trending market.
- A demo with a slick UI. The console exists but is utilitarian — built for the operator, not for screenshots.
- Finished. The [hexagonal bus refactor](decisions/hexagonal-bus.md) just landed across all 8 services; refdata REST snapshot capture is the next [bpt-tape](services/tape.md) extension; there's an [open backlog](https://github.com/bishanparktrading/bpt-core/blob/main/docs/backlog.md) of prod-hardening items.

---

## Where to start reading

If you have **5 minutes** → read [Architecture](architecture.md), then skim [Decisions](decisions/index.md).

If you have **20 minutes** → add one [Service deep-dive](services/index.md) that catches your eye (the [order gateway risk system](services/order-gateway.md) and [tape recording rig](services/tape.md) are the most interesting reads).

If you have **an hour** → clone the repo, run `bazel build //...`, and walk the
top-level service entry points: each `*/src/main.cpp` is short and the
`AeronBus::build()` call site shows the whole wiring graph.

---

## About

Built solo, in spare time, for live trading and to demonstrate I can think
about systems holistically (latency, fault tolerance, ops, risk) — not just write
clever code.

Background: ex-Millennium Partners (mlp-net, mlp-algo, mlpx). Reach out via [GitHub](https://github.com/bishanparktrading/bpt-core).

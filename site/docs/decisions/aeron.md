# Why Aeron

**Choice:** Aeron over shared memory (UDP IPC) for all in-host service-to-service messaging.

**Main alternatives considered:** nanomsg, ZeroMQ, Chronicle Queue, gRPC.

## What Aeron buys you

- **Sub-microsecond p50 latency** for IPC over shared memory (the same publish-subscribe transport that LMAX, Adaptive, and many trading firms use)
- **Mature backpressure** — log-buffer-based; the publisher gets back a position cursor and can detect slow subscribers without queueing in userspace
- **Multiple subscribers** per stream — the strategy, analytics, and the dashboard all consume the same MD stream without the publisher knowing
- **Battle-tested** — used in production at LMAX since ~2014, multiple HFT shops, plus the Real-Logic team is responsive
- **MIT licensed** — no commercial gotchas
- **Wire-protocol stability** — a 2018 wslog could be replayed by a 2026 subscriber without code changes

## What Aeron costs you

- **Java MediaDriver** — the daemon that brokers shared memory regions is a JVM. We wrap it in `bpt-transport` (Java + Gradle, built once, deployed as a fat JAR). Adds Java to the deploy footprint, which is otherwise C++.
- **Mental model overhead** — terms like "image", "subscription", "term length", "MTU" require some up-front study
- **Restart cascade** — if the MediaDriver dies, every subscriber sees its image disconnect. Mitigated via systemd `PartOf=bpt-transport.service` so all dependents auto-cascade-restart.

## Why not the alternatives

| Tool | Why not |
|---|---|
| **nanomsg** | Largely abandoned (last release 2019). Maintainer drama. ZeroMQ-shaped API but smaller community. |
| **ZeroMQ** | Decent for low-volume control planes, but no shared-memory transport — even `inproc://` is in-process, not cross-process. Adds a userland queueing layer on top of TCP/IPC sockets that hurts tail latency. |
| **Chronicle Queue** | Closest competitor in the trading space. Java-first; the C++ binding (`Chronicle Wire`) is a second-class citizen. Source-available license has commercial gotchas. |
| **gRPC** | Designed for cross-machine RPC over HTTP/2. Protobuf parse + HTTP/2 framing on every message. Three orders of magnitude slower than Aeron over shared memory. |
| **Plain shared-memory ringbuffer** | Always tempting; "I'll just write it myself." Then you spend two months on the back-pressure and replay semantics that Aeron has already solved. |
| **Kafka** | Designed for log-shaped data with seconds-to-minutes lag tolerance. Wrong fit for microsecond IPC. |

## What we actually use it for

```
bpt-md-gateway → bpt-strategy           # MD ticks
bpt-strategy → bpt-order-gateway        # orders
bpt-order-gateway → bpt-strategy        # exec reports + account snapshots
bpt-refdata → all consumers             # snapshot + delta
bpt-strategy → bpt-bridge               # portfolio snapshots for dashboard
bpt-pricer → bpt-strategy               # vol surface
bpt-analytics → bpt-strategy            # toxicity
```

7 distinct stream-IDs, all over `aeron:ipc` (single MediaDriver per host).
Multicast / unicast UDP transports are also wired but not used today — the
single-host topology is sufficient for the current scale.

## Hexagonal bus boundary

Even though Aeron is the chosen transport, no application code includes
`<Aeron.h>` outside of one `*AeronBus::build()` factory per service. See
[Hexagonal bus boundaries](hexagonal-bus.md) for why.

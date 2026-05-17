# bpt-pms

Multi-venue balance + position aggregator. Read-only — queries each configured
exchange's account endpoints and publishes normalised snapshots on Aeron. Owns
nothing that places orders, so a `bpt-pms` outage doesn't stop trading.

## What it does

- Periodic REST poll of `/account` (balances) and `/positions` (per venue)
- Normalises the per-venue shapes into a unified `AccountSnapshot` SBE message
- Publishes on the account-snapshot stream
- Consumed by `bpt-strategy` (for cross-venue position view) and the console

## Why a separate service

Balance/position queries are slow REST calls (100ms-2s typical), heavy on retry
logic, and not on any latency-sensitive path. Mixing them into the order gateway
would put REST IO on the same threads that handle exec parsing — bad for jitter.
Separation also means a venue's account endpoint flap doesn't affect the order
gateway's ability to receive new exec reports.

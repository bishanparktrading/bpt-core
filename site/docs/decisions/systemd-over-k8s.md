# systemd over Kubernetes

**Choice:** systemd user units + tarball-based deploy pipeline (`deploy.sh` /
`rollback.sh`).

**Main alternative considered:** Kubernetes (or Nomad).

## Why not k8s

For a solo-operator HFT stack, k8s buys very little and costs a lot:

**What it doesn't buy you:**
- *High availability* — trading is single-active by definition; you don't want two strategy instances racing to place orders
- *Auto-scaling* — strategy CPU is bounded by what the strategy needs, not by traffic
- *Service discovery* — services know each other's stream IDs at config-load time; nothing to discover
- *Rolling deploys* — you can't gracefully drain a strategy; you stop it, fix it, restart it

**What it costs you:**
- *Latency overhead* — k8s networking through CNI (Calico, Cilium) adds ~10-50µs to IPC. Aeron over `aeron:ipc` (shared memory) is sub-microsecond. The whole point of the architecture is destroyed.
- *Operational surface* — k8s is its own operating system. You'd need an SRE skillset just to keep the cluster healthy, before doing any actual trading work.
- *Dependency footprint* — etcd, control plane, ingress controller, CNI plugin. Things to break.

## What systemd buys you

- **Process supervision** — `Restart=on-failure`, `RestartSec=3`, `StartLimitBurst=5` — escapes restart loops, recovers from crashes
- **Resource limits** — `MemoryMax`, `TasksMax`, `LimitNOFILE`, `OOMScoreAdjust` — tiered by trading criticality (hot path -100, support -50, aux 0)
- **Lifecycle ordering** — `After=`, `Requires=`, `PartOf=` — bpt-strategy waits for bpt-md-gateway which waits for bpt-transport
- **Per-service identity** — `prctl(PR_SET_NAME)` + role-qualified service names (`bpt-strat-as`, `bpt-mdgw-okx`) so `top`, `journalctl`, log files all align
- **Cred mgmt** — `LoadCredentialEncrypted=` from systemd-creds, no static keys on the host
- **Cgroup isolation** — every service in its own cgroup; resource accounting + isolation without containers

## Deploy shape

```
release-vX.Y.Z.tgz
├── bin/                    # built binaries (Bazel + Gradle outputs, flat layout)
├── share/
│   ├── schema/             # SBE XML
│   ├── instruments/        # canonical mapping JSON
│   ├── service-configs/    # seed TOMLs (per service)
│   └── transport.yaml      # MediaDriver config
└── scripts/
    ├── package-release.sh
    ├── generate-units.sh   # emits systemd units, deploy-mode aware
    ├── deploy.sh           # atomic current/ symlink flip
    └── rollback.sh         # yo-yo current ↔ previous
```

`deploy.sh` flips a `current/` symlink atomically — old binaries keep running
out of `previous/` until restart (Linux resolves symlinks at `open()` time,
not continuously). Restarts pick up the new binary; rollback flips back.

## When this becomes the wrong call

- A second host doing a *different role* (recording — done; trading second venue — planned). Today this is per-host config; once we have ≥3 hosts the orchestration story matters more.
- Production hardening that genuinely needs declarative config — service mesh, secret rotation at scale, cross-host failover. We'd reach for **Nomad before k8s** at that point.
- A research grid for hyperparameter sweeps — that *is* a k8s-shaped problem (many short-lived jobs), but it lives outside the trading stack.

For now, systemd is the boring right answer.

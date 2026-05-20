# bpt-secmaster · pgweb systemd unit

Browseable read-only UI over the secmaster RDS Postgres, running as a
systemd unit on the trading host. Bound to `127.0.0.1:8081`; reachable
only via SSH tunnel.

## Why this shape

- **Trading host, not laptop.** The DSN credential lives on the trading
  host (encrypted via `systemd-creds`, host-bound). No DB creds on
  laptops.
- **127.0.0.1 only.** No public exposure. Access is via `ssh -L`.
- **`--readonly` flag.** Even if a curious operator opens the UI, all
  write actions are blocked by pgweb itself. Defense in depth: pair
  with a SELECT-only Postgres role later (`bpt_browse`) so the DB
  enforces it too.
- **systemd-creds for the DSN.** Same pattern as every other bpt
  service — see [[project_secrets_systemd_creds]] in operator memory.

## Layout

```
bpt-secmaster/systemd/
├── bpt-pgweb.service       systemd unit (sandboxed)
├── bpt-pgweb-start.sh      wrapper that reads DSN from credentials dir
└── install-pgweb.sh        one-time operator bootstrap
```

## First-time install

Run on the trading host (not the laptop):

```bash
# AWS CLI must be configured with creds that can read the secret.
# Quick check:
aws --region ap-northeast-1 secretsmanager describe-secret \
    --secret-id bpt-secmaster/db >/dev/null && echo OK

# Bootstrap (idempotent — safe to re-run for upgrades):
sudo ./install-pgweb.sh
```

The script:
1. Downloads pgweb (pinned to a version, x86_64 + arm64 supported).
2. Creates a dedicated `bpt-pgweb` system user.
3. Fetches the DSN from AWS Secrets Manager.
4. Encrypts the DSN via `systemd-creds encrypt` → host-bound `.cred` file.
5. Installs the wrapper + unit.
6. `systemctl enable + start`.

## Access from your laptop

```bash
ssh -L 8081:localhost:8081 trading-host
# Then in browser: http://localhost:8081
```

pgweb opens directly into the connection (no login screen — the URL was
already provided at startup). Default landing page shows the table list;
click `instrument`, `listing`, `exchange`, etc. to browse with sort/filter.

## Day-2 ops

- **Tail logs**: `sudo journalctl -u bpt-pgweb -f`
- **Restart**: `sudo systemctl restart bpt-pgweb`
- **Stop entirely**: `sudo systemctl disable --now bpt-pgweb`
- **Rotate the DSN credential** (after Secrets Manager rotation):
  `sudo ./install-pgweb.sh` (re-runs steps 3-6, leaves binary/unit alone)
- **Upgrade pgweb**: `PGWEB_VERSION=0.17.0 sudo ./install-pgweb.sh`

## Why no public exposure / no auth

pgweb has no native auth (sosedoff/pgweb#1). Options for direct internet
exposure would require:
- A reverse proxy (Caddy/nginx) with basic auth, or
- Cloudflare Access / Tailscale Funnel / SSO in front, or
- A WireGuard / Tailscale VPN

All add real ops surface for a tool used a handful of times a week.
SSH tunnel is the simplest secure access pattern and uses creds you
already have. Revisit if pgweb usage frequency justifies more polish.

## Tightening later (optional)

When you wire up Postgres role-based access (per
[[project_secmaster_roles]] follow-up):

```sql
-- Run as bptadmin once:
CREATE ROLE bpt_browse LOGIN PASSWORD 'xxx';
GRANT CONNECT ON DATABASE secmaster TO bpt_browse;
GRANT USAGE ON SCHEMA public TO bpt_browse;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO bpt_browse;
ALTER DEFAULT PRIVILEGES IN SCHEMA public
    GRANT SELECT ON TABLES TO bpt_browse;
```

Then re-generate the DSN for the `bpt_browse` role, store in a *separate*
Secrets Manager entry (`bpt-secmaster/db-browse`), and re-run
install-pgweb.sh with `SECRET_ID=bpt-secmaster/db-browse`. Now even a
pgweb compromise can't write to the DB.

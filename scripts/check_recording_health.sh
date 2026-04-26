#!/bin/bash
# check_recording_health.sh — recorder dead-man's-switch.
#
# Pings Healthchecks.io ($HC_URL) only when ALL of these are true:
#
#   1. bpt-md-recorder.service is active.
#   2. The latest .wslog file under $BPT_RAW_ROOT was written within
#      $STALE_THRESHOLD_SECONDS. A WS disconnect storm where the
#      recorder process stays up but writes nothing is the failure
#      mode this catches — systemd alone reports Active=true even
#      when no frames are arriving.
#
# If any check fails the script exits non-zero and skips the ping.
# Healthchecks.io's missed-ping window is configured to ~10 min there,
# so a single failure won't alert; sustained silence will.
#
# Required env (loaded from /etc/bpt/healthchecks.env via the unit):
#   HC_URL                 Healthchecks.io endpoint (UUID-keyed)
#
# Optional:
#   BPT_RAW_ROOT           default /opt/bpt/data/raw
#   STALE_THRESHOLD_SECONDS  default 600 (10 min). Tuned for HL APE
#                          where l2Book updates ≥ 1/sec under any
#                          remotely-active session — 10 min of
#                          nothing means the WS is dead.

set -euo pipefail

RAW_ROOT="${BPT_RAW_ROOT:-/opt/bpt/data/raw}"
STALE_THRESHOLD_SECONDS="${STALE_THRESHOLD_SECONDS:-600}"

log() { echo "[healthcheck $(date -u +%H:%M:%SZ)] $*" >&2; }

# 1. Service must be running.
if ! systemctl --user is-active --quiet bpt-md-recorder.service; then
    log "FAIL: bpt-md-recorder.service is not active"
    exit 1
fi

# 2. Latest .wslog mtime within threshold. find -printf to get an
#    epoch second so the comparison stays in a single number,
#    avoiding date-parse edge cases on different stat versions.
if [ ! -d "$RAW_ROOT" ]; then
    log "FAIL: raw root $RAW_ROOT missing"
    exit 1
fi

latest_mtime=$(find "$RAW_ROOT" -name '*.wslog' -printf '%T@\n' 2>/dev/null \
               | sort -nr | head -1 | cut -d. -f1)

if [ -z "$latest_mtime" ]; then
    log "FAIL: no .wslog files found under $RAW_ROOT"
    exit 1
fi

now=$(date +%s)
age=$(( now - latest_mtime ))

if [ "$age" -gt "$STALE_THRESHOLD_SECONDS" ]; then
    log "FAIL: latest .wslog is ${age}s old (threshold ${STALE_THRESHOLD_SECONDS}s)"
    exit 1
fi

log "ok: service active, latest wslog ${age}s ago"

# 3. All checks pass — ping HC.io. -fsS makes curl quiet on success
#    and exit non-zero on HTTP errors; --retry 3 covers a flaky
#    outbound link without amplifying outages.
if [ -z "${HC_URL:-}" ]; then
    log "WARN: HC_URL not set — skipping ping (passing checks would alert)"
    exit 0
fi

curl -fsS -m 10 --retry 3 "$HC_URL"

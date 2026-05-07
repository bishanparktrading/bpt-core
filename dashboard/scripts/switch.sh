#!/bin/bash
# switch.sh — One-command stack switcher for the dashboard.
#
# Tears down whatever stack is currently running (if any) and brings up the
# requested mode.  The browser tab stays connected to ws://localhost:8080
# throughout — when the new bridge comes up, the frontend auto-reconnects
# via its exponential-backoff loop and receives a fresh session message, so
# the mode pill flips automatically without a manual refresh.
#
# Usage:
#   ./switch.sh backtest [fenrir-config] [--starting-capital N] [--instrument-id N]
#   ./switch.sh paper    [fenrir-config] [--starting-capital N] [--instrument-id N]
#   ./switch.sh live     <fenrir-config> [--starting-capital N] [--instrument-id N]
#
# Examples:
#   ./switch.sh backtest fenrir/config/momentum.backtest.toml
#   ./switch.sh paper    fenrir/config/momentum.qa-okx.toml --instrument-id 200102
#   ./switch.sh live     fenrir/config/momentum.live-okx.toml
#
# Live mode inherits live_run.sh's "I UNDERSTAND" confirmation prompt — the
# switch is aborted if the user doesn't confirm.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SMOKE_TEST_SH="$SCRIPT_DIR/smoke_test.sh"
PAPER_RUN_SH="$SCRIPT_DIR/paper_run.sh"
LIVE_RUN_SH="$SCRIPT_DIR/live_run.sh"

MODE="${1:-}"
shift || true

if [ -z "$MODE" ]; then
    echo "Usage: $0 {backtest|paper|live} [fenrir-config] [--starting-capital N] [--instrument-id N]"
    exit 1
fi

case "$MODE" in
    backtest) TARGET_SCRIPT="$SMOKE_TEST_SH" ;;
    paper)    TARGET_SCRIPT="$PAPER_RUN_SH"  ;;
    live)     TARGET_SCRIPT="$LIVE_RUN_SH"   ;;
    *)
        echo "Unknown mode: $MODE (expected backtest|paper|live)"
        exit 1
        ;;
esac

echo "=== Dashboard mode switch → $MODE ==="
echo

# ── 1. Tear down whatever's running ─────────────────────────────────────────
# All three stop commands are idempotent and clean up the same set of
# services, so calling smoke_test.sh stop is safe regardless of which stack
# was actually running.  (It also knows to stop bpt-backtester, which the other
# two don't — important because backtest→paper needs that teardown.)
echo "--- Stopping current stack (if any) ---"
"$SMOKE_TEST_SH" stop 2>&1 || true
echo

# ── 2. Start the new stack ──────────────────────────────────────────────────
echo "--- Starting $MODE stack ---"
exec "$TARGET_SCRIPT" start "$@"

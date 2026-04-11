#!/bin/bash
# live_run.sh — Launch the LIVE trading stack + dashboard bridge.
#
# DANGER: real orders, real money.  The bridge broadcasts mode="live"
# so the dashboard top-bar shows a pulsing red LIVE pill — impossible
# to miss at a glance.
#
# Usage:
#   ./live_run.sh start <fenrir-config> [--starting-capital N]
#   ./live_run.sh stop
#   ./live_run.sh status

set -euo pipefail

STACK_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BRIDGE_BIN="$STACK_DIR/build/dashboard/bridge/bridge"
BRIDGE_CFG="$STACK_DIR/dashboard/bridge/config/bridge.backtest.toml"
BRIDGE_LOG_DIR="$STACK_DIR/dashboard/bridge/logs"
BRIDGE_PID="$STACK_DIR/dashboard/bridge/.bridge.pid"
LIVE_SH="$STACK_DIR/scripts/live.sh"
FRONTEND_DIR="$STACK_DIR/dashboard/frontend"

is_running() {
    local pid_file="$1"
    [ -f "$pid_file" ] && kill -0 "$(cat "$pid_file")" 2>/dev/null
}

derive_strategy_name() {
    local cfg="$1"
    [ -z "$cfg" ] && { echo "unknown"; return; }
    local base
    base="$(basename "$cfg")"
    base="${base%%.live-okx.toml}"
    base="${base%%.*}"
    awk 'BEGIN{FS="_"; OFS=""}
         { for (i=1;i<=NF;i++) $i=toupper(substr($i,1,1)) substr($i,2); print $0 "Strategy" }' <<<"$base"
}

bridge_start() {
    if is_running "$BRIDGE_PID"; then
        echo "  [UP]   bridge (PID $(cat "$BRIDGE_PID")) — already running"
        return 0
    fi

    if [ ! -x "$BRIDGE_BIN" ]; then
        echo "ERROR: bridge binary not found at $BRIDGE_BIN"
        exit 1
    fi

    mkdir -p "$BRIDGE_LOG_DIR"

    local strategy_name
    strategy_name="$(derive_strategy_name "${FENRIR_CONFIG_OVERRIDE:-}")"

    local cap_args=()
    if [ -n "${STARTING_CAPITAL:-}" ]; then
        cap_args=(--starting-capital "$STARTING_CAPITAL")
    fi

    echo "  Starting bridge (mode: LIVE, strategy: $strategy_name${STARTING_CAPITAL:+, starting_capital: \$$STARTING_CAPITAL})..."
    nohup "$BRIDGE_BIN" --config "$BRIDGE_CFG" \
                        --mode live \
                        --strategy-name "$strategy_name" \
                        "${cap_args[@]}" \
        > "$BRIDGE_LOG_DIR/bridge.stdout" 2>&1 &
    echo $! > "$BRIDGE_PID"

    sleep 1
    if is_running "$BRIDGE_PID"; then
        echo "  [UP]   bridge (PID $(cat "$BRIDGE_PID"))"
    else
        echo "  [FAIL] bridge did not start — check $BRIDGE_LOG_DIR/bridge.stdout"
        rm -f "$BRIDGE_PID"
        exit 1
    fi
}

bridge_stop() {
    if is_running "$BRIDGE_PID"; then
        local pid
        pid=$(cat "$BRIDGE_PID")
        echo "  Stopping bridge (PID $pid)..."
        kill "$pid" 2>/dev/null || true
        for _ in 1 2 3 4 5; do
            is_running "$BRIDGE_PID" || break
            sleep 0.5
        done
        is_running "$BRIDGE_PID" && kill -9 "$pid" 2>/dev/null || true
    fi
    rm -f "$BRIDGE_PID"
    echo "  [DOWN] bridge"
}

do_status() {
    "$LIVE_SH" status
    if is_running "$BRIDGE_PID"; then
        echo "  [UP]   bridge (PID $(cat "$BRIDGE_PID"))"
    else
        echo "  [DOWN] bridge"
    fi
}

do_start() {
    if [ -z "$FENRIR_CONFIG_OVERRIDE" ]; then
        echo "ERROR: live_run.sh requires an explicit fenrir config."
        echo "Usage: $0 start <fenrir-config> [--starting-capital N]"
        exit 1
    fi

    echo "=== Dashboard LIVE trading run — starting ==="
    echo

    # live.sh enforces its own confirmation prompt — if the user aborts it
    # there, nothing else starts.
    "$LIVE_SH" start "$FENRIR_CONFIG_OVERRIDE"
    echo

    bridge_start
    echo

    cat <<EOF
=== LIVE trading stack is up ===

In a separate terminal, start the frontend pointed at the bridge:

    cd $FRONTEND_DIR
    VITE_WS_URL=ws://localhost:8080 npm run dev

The top bar will show a pulsing RED LIVE pill.  Every fill in the
blotter is a real trade against real money.

Logs:
  bridge  : $BRIDGE_LOG_DIR/bridge.stdout
  fenrir  : $STACK_DIR/fenrir/logs/fenrir.log
EOF
}

do_stop() {
    echo "=== Dashboard LIVE trading run — stopping ==="
    bridge_stop
    "$LIVE_SH" stop
    echo "=== Stack is down ==="
}

# ── Arg parsing ────────────────────────────────────────────────────────────
SUBCMD="${1:-}"
shift || true

FENRIR_CONFIG_OVERRIDE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --starting-capital)   STARTING_CAPITAL="$2"; shift 2 ;;
        --starting-capital=*) STARTING_CAPITAL="${1#--starting-capital=}"; shift ;;
        -*)                   echo "Unknown flag: $1"; exit 1 ;;
        *)
            if [ -z "$FENRIR_CONFIG_OVERRIDE" ]; then
                FENRIR_CONFIG_OVERRIDE="$1"
            fi
            shift
            ;;
    esac
done
export STARTING_CAPITAL

case "$SUBCMD" in
    start)  do_start ;;
    stop)   do_stop ;;
    status) do_status ;;
    *)
        echo "Usage: $0 start <fenrir-config> [--starting-capital N]"
        echo "       $0 stop"
        echo "       $0 status"
        exit 1
        ;;
esac

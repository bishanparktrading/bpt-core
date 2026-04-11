#!/bin/bash
# live.sh — Run the Fenrir LIVE trading stack against OKX mainnet.
#
#   ██╗     ██╗██╗   ██╗███████╗
#   ██║     ██║██║   ██║██╔════╝
#   ██║     ██║██║   ██║█████╗
#   ██║     ██║╚██╗ ██╔╝██╔══╝
#   ███████╗██║ ╚████╔╝ ███████╗
#   ╚══════╝╚═╝  ╚═══╝  ╚══════╝
#
# This script places REAL orders with REAL money against the exchange's
# mainnet endpoints.  Fills will settle on-chain / on-exchange and cannot
# be reversed.
#
# Before first use, create these config files pointing at mainnet endpoints
# and live credentials:
#
#   muninn/config/muninn.live-okx.toml
#   huginn/config/huginn.live-okx.toml
#   heimdall/config/heimdall.live-okx.toml
#   fenrir/config/<strategy>.live-okx.toml
#
# Usage:
#   ./live.sh start <fenrir-config>   REQUIRED — no default, explicit only.
#   ./live.sh stop                    Stop all services.
#   ./live.sh status                  Show running state.

set -euo pipefail

STACK_DIR="$(cd "$(dirname "$0")/.." && pwd)"

BIFROST_DIR="$STACK_DIR/bifrost/fabric"
MUNINN_DIR="$STACK_DIR/muninn"
HUGINN_DIR="$STACK_DIR/huginn"
HEIMDALL_DIR="$STACK_DIR/heimdall"
FENRIR_DIR="$STACK_DIR/fenrir"

FENRIR_CONFIG="${2:-}"  # no default — live trading requires explicit choice
MUNINN_CONFIG="$MUNINN_DIR/config/muninn.live-okx.toml"
HUGINN_CONFIG="$HUGINN_DIR/config/huginn.live-okx.toml"
HEIMDALL_CONFIG="$HEIMDALL_DIR/config/heimdall.live-okx.toml"

# ── Helpers ───────────────────────────────────────────────────────

is_running() {
    local pid_file="$1"
    [ -f "$pid_file" ] && kill -0 "$(cat "$pid_file")" 2>/dev/null
}

service_status() {
    local name="$1"
    local pid_file="$2"
    if is_running "$pid_file"; then
        echo "  [UP]   $name (PID $(cat "$pid_file"))"
    else
        echo "  [DOWN] $name"
    fi
}

do_status() {
    echo "LIVE trading stack status:"
    service_status "bifrost-fabric" "$BIFROST_DIR/.bifrost.pid"
    service_status "muninn"         "$MUNINN_DIR/.muninn.pid"
    service_status "huginn"         "$HUGINN_DIR/.huginn.pid"
    service_status "heimdall"       "$HEIMDALL_DIR/.heimdall.pid"
    service_status "fenrir"         "$FENRIR_DIR/.fenrir.pid"
}

check_preflight() {
    local missing=()
    [ -f "$MUNINN_CONFIG"   ] || missing+=("$MUNINN_CONFIG")
    [ -f "$HUGINN_CONFIG"   ] || missing+=("$HUGINN_CONFIG")
    [ -f "$HEIMDALL_CONFIG" ] || missing+=("$HEIMDALL_CONFIG")
    [ -z "$FENRIR_CONFIG"   ] && missing+=("<fenrir-config arg>")
    [ -n "$FENRIR_CONFIG" ] && [ ! -f "$FENRIR_CONFIG" ] && missing+=("$FENRIR_CONFIG")

    if [ ${#missing[@]} -gt 0 ]; then
        echo "ERROR: cannot start LIVE stack — missing:"
        for m in "${missing[@]}"; do echo "  - $m"; done
        echo
        echo "Live trading requires explicit config files for every service."
        echo "Copy the qa-okx variants and edit them to point at mainnet endpoints"
        echo "and live credentials before using this script."
        exit 1
    fi
}

confirm() {
    echo
    echo "╔══════════════════════════════════════════════════════════════╗"
    echo "║  YOU ARE ABOUT TO START LIVE TRADING AGAINST OKX MAINNET    ║"
    echo "║  Real orders.  Real money.  Fills cannot be reversed.       ║"
    echo "╚══════════════════════════════════════════════════════════════╝"
    echo
    echo "  Fenrir config : $FENRIR_CONFIG"
    echo
    read -r -p "Type 'I UNDERSTAND' to continue, anything else to abort: " reply
    if [ "$reply" != "I UNDERSTAND" ]; then
        echo "Aborted."
        exit 1
    fi
}

do_start() {
    check_preflight
    confirm

    echo "=== Starting Fenrir LIVE trading stack (OKX mainnet) ==="
    echo

    "$BIFROST_DIR/scripts/dev_start.sh"
    echo
    "$MUNINN_DIR/scripts/start.sh" "$MUNINN_CONFIG"
    echo

    "$HUGINN_DIR/scripts/start.sh" "$HUGINN_CONFIG" &
    HUGINN_PID=$!
    "$HEIMDALL_DIR/scripts/start.sh" "$HEIMDALL_CONFIG" &
    HEIMDALL_PID=$!
    wait "$HUGINN_PID"
    wait "$HEIMDALL_PID"
    echo

    "$FENRIR_DIR/scripts/start.sh" "$FENRIR_CONFIG"
    echo

    echo "=== LIVE trading stack is up — Fenrir is trading with real money ==="
    echo
    do_status
    echo
    echo "Stop with: $0 stop"
}

do_stop() {
    echo "=== Stopping LIVE trading stack ==="
    "$FENRIR_DIR/scripts/stop.sh"      2>/dev/null || true
    "$HEIMDALL_DIR/scripts/stop.sh"    2>/dev/null || true
    "$HUGINN_DIR/scripts/stop.sh"      2>/dev/null || true
    "$MUNINN_DIR/scripts/stop.sh"      2>/dev/null || true
    "$BIFROST_DIR/scripts/dev_stop.sh" 2>/dev/null || true
    echo "=== LIVE trading stack is down ==="
}

case "${1:-}" in
    start)  do_start ;;
    stop)   do_stop ;;
    status) do_status ;;
    *)
        echo "Usage: $0 start <fenrir-config>"
        echo "       $0 stop"
        echo "       $0 status"
        exit 1
        ;;
esac

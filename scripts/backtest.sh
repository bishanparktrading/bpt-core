#!/bin/bash
# backtest.sh — Run a Fenrir backtest against Jormungandr.
#
# Jormungandr acts as the exchange simulator behind Huginn and Heimdall.
# Fenrir runs in its normal live mode — no backtest-specific code paths.
# All inter-service communication still flows through Bifrost (Aeron IPC).
#
# Usage:
#   ./backtest.sh start [fenrir-config]   Start the backtest stack.
#   ./backtest.sh stop                    Stop all services.
#   ./backtest.sh status                  Show running state.
#
# The optional fenrir-config argument overrides the default strategy config.
# Default: fenrir/config/vwap_reversion.qa-okx.toml

set -euo pipefail

STACK_DIR="$(cd "$(dirname "$0")/.." && pwd)"

BIFROST_DIR="$STACK_DIR/bifrost/fabric"
MUNINN_DIR="$STACK_DIR/muninn"
HUGINN_DIR="$STACK_DIR/huginn"
HEIMDALL_DIR="$STACK_DIR/heimdall"
FENRIR_DIR="$STACK_DIR/fenrir"
JORMUNGANDR_DIR="$STACK_DIR/jormungandr"

FENRIR_CONFIG="${2:-$FENRIR_DIR/config/vwap_reversion.backtest.toml}"
JORMUNGANDR_CONFIG="$JORMUNGANDR_DIR/config/jormungandr.qa-okx.toml"

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
    echo "Backtest stack status:"
    service_status "bifrost-fabric" "$BIFROST_DIR/.bifrost.pid"
    service_status "muninn"         "$MUNINN_DIR/.muninn.pid"
    service_status "jormungandr"    "$JORMUNGANDR_DIR/.jormungandr.pid"
    service_status "huginn"         "$HUGINN_DIR/.huginn.pid"
    service_status "heimdall"       "$HEIMDALL_DIR/.heimdall.pid"
    service_status "fenrir"         "$FENRIR_DIR/.fenrir.pid"
}

do_start() {
    echo "=== Starting Fenrir backtest stack ==="
    echo "  Fenrir config      : $FENRIR_CONFIG"
    echo "  Jormungandr config : $JORMUNGANDR_CONFIG"
    echo

    # 1. Bifrost-fabric — Aeron media driver must be up first
    "$BIFROST_DIR/scripts/dev_start.sh"
    echo

    # 2. Muninn — reference data (connects to live exchanges for canonical IDs)
    "$MUNINN_DIR/scripts/start.sh" "$MUNINN_DIR/config/muninn.qa-okx.toml"
    echo

    # 3. Huginn + Heimdall — connect to Jormungandr WS servers once Jormungandr is up.
    #    Start them now so they are in reconnect-retry loop when Jormungandr launches.
    "$HUGINN_DIR/scripts/start.sh" "$HUGINN_DIR/config/huginn.backtest.toml" &
    HUGINN_PID=$!

    "$HEIMDALL_DIR/scripts/start.sh" "$HEIMDALL_DIR/config/heimdall.backtest.toml" &
    HEIMDALL_PID=$!

    wait "$HUGINN_PID"
    wait "$HEIMDALL_PID"
    echo

    # 4. Fenrir — must be up and have sent MD subscription requests to Huginn before
    #    Jormungandr starts feeding data. Fenrir waits for RefDataReady from Muninn, then
    #    subscribes. Give it time to settle so Huginn has a non-empty subscription list.
    "$FENRIR_DIR/scripts/start.sh" "$FENRIR_CONFIG"
    echo

    echo "Waiting 15s for Fenrir to subscribe to instruments via Huginn..."
    sleep 15

    # 5. Jormungandr — start last so that Huginn already has subscriptions queued.
    #    The subscriber gate fires when Huginn reconnects with a non-empty subscription list.
    "$JORMUNGANDR_DIR/scripts/start.sh" "$JORMUNGANDR_CONFIG"
    echo

    echo "=== Backtest stack is up — waiting for Jormungandr to exhaust data ==="
    echo
    do_status
    echo
    echo "Logs:"
    echo "  tail -f $JORMUNGANDR_DIR/logs/backtest/jormungandr.log"
    echo "  tail -f $FENRIR_DIR/logs/fenrir.log"
    echo
    echo "Results will be written to the output_dir configured in:"
    echo "  $JORMUNGANDR_CONFIG"
}

do_stop() {
    echo "=== Stopping backtest stack ==="
    "$FENRIR_DIR/scripts/stop.sh"        2>/dev/null || true
    "$HEIMDALL_DIR/scripts/stop.sh"      2>/dev/null || true
    "$HUGINN_DIR/scripts/stop.sh"        2>/dev/null || true
    "$JORMUNGANDR_DIR/scripts/stop.sh"   2>/dev/null || true
    "$MUNINN_DIR/scripts/stop.sh"        2>/dev/null || true
    "$BIFROST_DIR/scripts/dev_stop.sh"   2>/dev/null || true
    echo "=== Backtest stack is down ==="
}

case "${1:-}" in
    start)  do_start ;;
    stop)   do_stop ;;
    status) do_status ;;
    *)
        echo "Usage: $0 {start|stop|status} [fenrir-config]"
        exit 1
        ;;
esac

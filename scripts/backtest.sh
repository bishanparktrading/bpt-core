#!/bin/bash
# backtest.sh — Run a Strategy backtest against Backtester.
#
# Backtester acts as the exchange simulator behind bpt-md-gateway and bpt-order-gateway.
# Strategy runs in its normal live mode — no backtest-specific code paths.
# All inter-service communication still flows through bpt-transport (Aeron IPC).
#
# Usage:
#   ./backtest.sh start [bpt-strategy-config]   Start the backtest stack.
#   ./backtest.sh stop                    Stop all services.
#   ./backtest.sh status                  Show running state.
#
# The optional bpt-strategy-config argument overrides the default strategy config.
# Default: bpt-strategy/config/vwap_reversion.qa-okx.toml
#
# Env vars:
#   STARTING_CAPITAL   Override bpt-backtester's starting_capital (default: TOML value).

set -euo pipefail

STACK_DIR="$(cd "$(dirname "$0")/.." && pwd)"

TRANSPORT_DIR="$STACK_DIR/transport/aeron"
REFDATA_DIR="$STACK_DIR/bpt-refdata"
MD_GATEWAY_DIR="$STACK_DIR/bpt-md-gateway"
ORDER_GATEWAY_DIR="$STACK_DIR/bpt-order-gateway"
STRATEGY_DIR="$STACK_DIR/bpt-strategy"
BACKTESTER_DIR="$STACK_DIR/bpt-backtester"

STRATEGY_CONFIG="${2:-$STRATEGY_DIR/config/vwap_reversion.backtest.toml}"
BACKTESTER_CONFIG="$BACKTESTER_DIR/config/bpt-backtester.qa-okx.toml"

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
    service_status "transport" "$TRANSPORT_DIR/.bpt-transport.pid"
    service_status "bpt-refdata"         "$REFDATA_DIR/.bpt-refdata.pid"
    service_status "bpt-backtester"    "$BACKTESTER_DIR/.bpt-backtester.pid"
    service_status "bpt-md-gateway"         "$MD_GATEWAY_DIR/.bpt-md-gateway.pid"
    service_status "order-gateway"       "$ORDER_GATEWAY_DIR/.order-gateway.pid"
    service_status "bpt-strategy"         "$STRATEGY_DIR/.bpt-strategy.pid"
}

do_start() {
    echo "=== Starting Strategy backtest stack ==="
    echo "  Strategy config      : $STRATEGY_CONFIG"
    echo "  Backtester config : $BACKTESTER_CONFIG"
    echo

    # 1. bpt-transport — Aeron media driver must be up first
    "$TRANSPORT_DIR/scripts/dev_start.sh"
    echo

    # 2. Refdata — reference data. Default = HL config because AS backtests
    # target HL APE. Override via $BPT_REFDATA_CONFIG when backtesting other venues.
    "$REFDATA_DIR/scripts/start.sh" "${BPT_REFDATA_CONFIG:-$REFDATA_DIR/config/bpt-refdata.qa-hyperliquid.toml}"
    echo

    # 3. bpt-md-gateway + bpt-order-gateway — connect to Backtester WS servers once Backtester is up.
    #    Start them now so they are in reconnect-retry loop when Backtester launches.
    "$MD_GATEWAY_DIR/scripts/start.sh" "$MD_GATEWAY_DIR/config/bpt-md-gateway.backtest.toml" &
    MD_GATEWAY_PID=$!

    "$ORDER_GATEWAY_DIR/scripts/start.sh" "$ORDER_GATEWAY_DIR/config/bpt-order-gateway.backtest.toml" &
    ORDER_GATEWAY_PID=$!

    wait "$MD_GATEWAY_PID"
    wait "$ORDER_GATEWAY_PID"
    echo

    # 4. Strategy — must be up and have sent MD subscription requests to bpt-md-gateway before
    #    Backtester starts feeding data. Strategy waits for RefDataReady from Refdata, then
    #    subscribes. Give it time to settle so bpt-md-gateway has a non-empty subscription list.
    "$STRATEGY_DIR/scripts/start.sh" "$STRATEGY_CONFIG"
    echo

    echo "Waiting 15s for Strategy to subscribe to instruments via bpt-md-gateway..."
    sleep 15

    # 5. Backtester — start last so that bpt-md-gateway already has subscriptions queued.
    #    The subscriber gate fires when bpt-md-gateway reconnects with a non-empty subscription list.

    # Run identity passed through to the backtester:
    #   strategy_name = filename stem before the first dot
    #     (avellaneda_stoikov.backtest.toml → AvellanedaStoikov, formatted CamelCase)
    #   params_hash   = sha256 of the strategy config bytes
    #   git_sha       = repo HEAD
    # All three end up in summary.json + the on-disk run_id so the archive
    # tooling can identify "this strategy + these params + this commit"
    # without ambiguity.
    STRATEGY_BASENAME="$(basename "$STRATEGY_CONFIG")"
    STRATEGY_STEM="${STRATEGY_BASENAME%%.*}"
    STRATEGY_NAME="$(echo "$STRATEGY_STEM" \
        | awk -F_ '{ for (i=1;i<=NF;i++) printf "%s%s", toupper(substr($i,1,1)), substr($i,2); print "" }')"
    PARAMS_HASH="$(sha256sum "$STRATEGY_CONFIG" | awk '{print $1}')"
    GIT_SHA="$(git -C "$STACK_DIR" rev-parse HEAD 2>/dev/null || echo unknown)"

    # Resolve the leaf params file (the strategy config has a
    # `strategy_config = "strategies/...toml"` pointer to it). Falls back
    # to the instance config itself when no pointer exists, so older
    # one-file strategies still get a params.toml copied into the run dir.
    PARAMS_REL="$(awk -F= '/^strategy_config/ { gsub(/[ \t"]/, "", $2); print $2 }' "$STRATEGY_CONFIG" | head -1)"
    if [ -n "$PARAMS_REL" ]; then
        PARAMS_FILE="$(cd "$(dirname "$STRATEGY_CONFIG")" && pwd)/$PARAMS_REL"
    else
        PARAMS_FILE="$STRATEGY_CONFIG"
    fi

    EXTRA="--strategy-name $STRATEGY_NAME --params-hash $PARAMS_HASH --git-sha $GIT_SHA --params-file $PARAMS_FILE"
    if [ -n "${STARTING_CAPITAL:-}" ]; then
        echo "  (starting_capital override: \$$STARTING_CAPITAL)"
        EXTRA="$EXTRA --starting-capital $STARTING_CAPITAL"
    fi
    echo "  Run identity:"
    echo "    strategy_name : $STRATEGY_NAME"
    echo "    params_hash   : ${PARAMS_HASH:0:8}…"
    echo "    git_sha       : ${GIT_SHA:0:7}"
    BACKTESTER_EXTRA_ARGS="$EXTRA" "$BACKTESTER_DIR/scripts/start.sh" "$BACKTESTER_CONFIG"
    echo

    echo "=== Backtest stack is up — waiting for Backtester to exhaust data ==="
    echo
    do_status
    echo
    echo "Logs:"
    echo "  tail -f $BACKTESTER_DIR/logs/backtest/bpt-backtester.log"
    echo "  tail -f $STRATEGY_DIR/logs/bpt-strategy.log"
    echo
    echo "Results will be written to the output_dir configured in:"
    echo "  $BACKTESTER_CONFIG"
}

do_stop() {
    echo "=== Stopping backtest stack ==="
    "$STRATEGY_DIR/scripts/stop.sh"        2>/dev/null || true
    "$ORDER_GATEWAY_DIR/scripts/stop.sh"      2>/dev/null || true
    "$MD_GATEWAY_DIR/scripts/stop.sh"        2>/dev/null || true
    "$BACKTESTER_DIR/scripts/stop.sh"   2>/dev/null || true
    "$REFDATA_DIR/scripts/stop.sh"        2>/dev/null || true
    "$TRANSPORT_DIR/scripts/dev_stop.sh"   2>/dev/null || true
    echo "=== Backtest stack is down ==="
}

case "${1:-}" in
    start)  do_start ;;
    stop)   do_stop ;;
    status) do_status ;;
    *)
        echo "Usage: $0 {start|stop|status} [bpt-strategy-config]"
        exit 1
        ;;
esac

#!/bin/bash
# One-time bootstrap of pgweb on the trading host.
#
# Run as root (or via sudo) on the trading host.
#
# Prerequisites (operator-side, before running this):
#   1. AWS CLI installed + configured with creds that have
#      secretsmanager:GetSecretValue on the bpt-secmaster/db secret.
#   2. systemd ≥ 250 (for `systemd-creds encrypt`).
#   3. curl + unzip.
#
# Override defaults via env vars: PGWEB_VERSION, SECRET_ID, AWS_REGION,
# SERVICE_USER.

set -euo pipefail

PGWEB_VERSION="${PGWEB_VERSION:-0.16.2}"
SECRET_ID="${SECRET_ID:-bpt-secmaster/db}"
AWS_REGION="${AWS_REGION:-ap-northeast-1}"
SERVICE_USER="${SERVICE_USER:-bpt-pgweb}"

if [[ $EUID -ne 0 ]]; then
    echo "Must run as root (sudo)" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── 1. Install pgweb binary ───────────────────────────────────────────
if [[ -x /usr/local/bin/pgweb ]] && /usr/local/bin/pgweb --version 2>/dev/null | grep -q "$PGWEB_VERSION"; then
    echo "==> pgweb $PGWEB_VERSION already installed"
else
    echo "==> Installing pgweb $PGWEB_VERSION"
    arch=$(uname -m)
    case "$arch" in
        x86_64)  pgweb_arch="linux_amd64" ;;
        aarch64) pgweb_arch="linux_arm64" ;;
        *) echo "unsupported arch: $arch"; exit 1 ;;
    esac
    tmp=$(mktemp -d)
    trap "rm -rf $tmp" EXIT
    curl -fsSL \
      "https://github.com/sosedoff/pgweb/releases/download/v${PGWEB_VERSION}/pgweb_${pgweb_arch}.zip" \
      -o "$tmp/pgweb.zip"
    unzip -q -o "$tmp/pgweb.zip" -d "$tmp"
    install -m 0755 "$tmp/pgweb_${pgweb_arch}" /usr/local/bin/pgweb
fi

# ── 2. Create dedicated service user ──────────────────────────────────
if ! id "$SERVICE_USER" &>/dev/null; then
    echo "==> Creating service user $SERVICE_USER"
    useradd --system --no-create-home --shell /usr/sbin/nologin "$SERVICE_USER"
fi

# ── 3. Fetch DSN from Secrets Manager ─────────────────────────────────
echo "==> Fetching DSN from Secrets Manager (secret=$SECRET_ID region=$AWS_REGION)"
secret_json=$(aws --region "$AWS_REGION" secretsmanager get-secret-value \
    --secret-id "$SECRET_ID" --query SecretString --output text)
dsn=$(python3 -c "import sys, json; print(json.loads(sys.stdin.read())['dsn'])" <<< "$secret_json")

if [[ -z "$dsn" ]]; then
    echo "fatal: DSN missing from secret payload" >&2
    exit 1
fi

# ── 4. Encrypt with systemd-creds (host-bound) ────────────────────────
echo "==> Encrypting DSN with systemd-creds → /etc/bpt/creds/bpt-secmaster-dsn.cred"
mkdir -p /etc/bpt/creds
chmod 0700 /etc/bpt/creds
# systemd-creds reads stdin, encrypts with the host's TPM/host key,
# writes the .cred file. Only this exact host can decrypt it.
echo -n "$dsn" | systemd-creds encrypt --name=secmaster-dsn - /etc/bpt/creds/bpt-secmaster-dsn.cred
chmod 0600 /etc/bpt/creds/bpt-secmaster-dsn.cred

# Scrub the plaintext from this process's memory + shell history scope.
unset dsn secret_json

# ── 5. Install wrapper + unit ─────────────────────────────────────────
echo "==> Installing /usr/local/bin/bpt-pgweb-start"
install -m 0755 "$SCRIPT_DIR/bpt-pgweb-start.sh" /usr/local/bin/bpt-pgweb-start

echo "==> Installing /etc/systemd/system/bpt-pgweb.service"
install -m 0644 "$SCRIPT_DIR/bpt-pgweb.service" /etc/systemd/system/bpt-pgweb.service

# ── 6. Enable + start ─────────────────────────────────────────────────
echo "==> Reloading systemd, enabling + starting unit"
systemctl daemon-reload
systemctl enable bpt-pgweb.service
systemctl restart bpt-pgweb.service

sleep 2
if systemctl is-active --quiet bpt-pgweb.service; then
    echo ""
    echo "✓ pgweb installed and running on 127.0.0.1:8081"
    echo ""
    echo "  Tunnel from your laptop:"
    echo "    ssh -L 8081:localhost:8081 $(hostname)"
    echo "  Then open:"
    echo "    http://localhost:8081"
    echo ""
    echo "  Logs: journalctl -u bpt-pgweb -f"
else
    echo "✗ Unit failed to start. Check: journalctl -u bpt-pgweb --no-pager -n 50" >&2
    exit 1
fi

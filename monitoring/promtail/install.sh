#!/bin/bash
# Install promtail on a bpt host. Tails systemd journal + ships to Loki.
#
# Usage:
#   sudo BPT_HOST_NAME=sg1-trd-test-01 \
#        BPT_HOST_ROLE=trd \
#        BPT_HOST_ENV=test \
#        BPT_LOKI_URL=http://100.x.y.z:3100 \
#        bash install.sh
#
# Run-time inputs (env vars):
#   BPT_HOST_NAME   hostname per the <loc>-<role>-<env>-<seq> convention
#   BPT_HOST_ROLE   trd / rec / mon / ctl / rsh
#   BPT_HOST_ENV    test / qa / prod
#   BPT_LOKI_URL    http://<monitor-host-tailscale-ip>:3100

set -euxo pipefail

: "${BPT_HOST_NAME:?BPT_HOST_NAME required}"
: "${BPT_HOST_ROLE:?BPT_HOST_ROLE required}"
: "${BPT_HOST_ENV:?BPT_HOST_ENV required}"
: "${BPT_LOKI_URL:?BPT_LOKI_URL required}"

PROMTAIL_VERSION="${PROMTAIL_VERSION:-3.2.0}"

# ── Download binary ────────────────────────────────────────────────────────
if [[ ! -x /usr/local/bin/promtail ]]; then
    cd /tmp
    curl -fL -o promtail.zip \
        "https://github.com/grafana/loki/releases/download/v${PROMTAIL_VERSION}/promtail-linux-amd64.zip"
    unzip -o promtail.zip
    install -m 0755 promtail-linux-amd64 /usr/local/bin/promtail
    rm -f promtail.zip promtail-linux-amd64
fi

# ── Config + positions dir ────────────────────────────────────────────────
install -d /etc/promtail /var/lib/promtail /var/log/promtail
install -m 0644 "$(dirname "$0")/promtail.yml" /etc/promtail/promtail.yml

# ── Systemd unit ──────────────────────────────────────────────────────────
# Runs as root so it can read /var/log/journal — promtail joins the
# systemd-journal group otherwise but root is simpler at one-host scale.
# EnvironmentFile holds the per-host vars (gitignored).
cat > /etc/promtail/promtail.env <<EOF
BPT_HOST_NAME=${BPT_HOST_NAME}
BPT_HOST_ROLE=${BPT_HOST_ROLE}
BPT_HOST_ENV=${BPT_HOST_ENV}
BPT_LOKI_URL=${BPT_LOKI_URL}
EOF
chmod 0600 /etc/promtail/promtail.env

cat > /etc/systemd/system/promtail.service <<'EOF'
[Unit]
Description=Promtail — ships systemd journal to Loki
Documentation=https://grafana.com/docs/loki/latest/clients/promtail/
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=root
EnvironmentFile=/etc/promtail/promtail.env
ExecStart=/usr/local/bin/promtail -config.file=/etc/promtail/promtail.yml -config.expand-env=true
Restart=on-failure
RestartSec=5s
# Capabilities: needs CAP_DAC_READ_SEARCH to read /var/log/journal across
# all units. Could tighten to a dedicated user in the journal group if
# we hit a security review.

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable promtail
systemctl restart promtail

# ── Quick smoke test ──────────────────────────────────────────────────────
sleep 3
if systemctl is-active --quiet promtail; then
    echo "── promtail active. Verify shipping with:"
    echo "    journalctl -u promtail -n 20"
    echo "    curl -s ${BPT_LOKI_URL}/ready"
else
    echo "── promtail FAILED to start. Diagnose with:"
    echo "    journalctl -u promtail -n 50 --no-pager"
    exit 1
fi

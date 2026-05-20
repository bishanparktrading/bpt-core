#!/bin/bash
# Wrapper script invoked by the bpt-pgweb.service unit.
# Reads the encrypted DSN from $CREDENTIALS_DIRECTORY (populated by
# systemd-creds at unit start) and execs pgweb.
#
# Bound to 127.0.0.1 only — access is via SSH tunnel from the operator's
# laptop, never directly from the internet.

set -euo pipefail

if [[ -z "${CREDENTIALS_DIRECTORY:-}" ]]; then
    echo "fatal: CREDENTIALS_DIRECTORY not set — must be invoked by systemd with LoadCredentialEncrypted=" >&2
    exit 1
fi

DSN_FILE="$CREDENTIALS_DIRECTORY/secmaster-dsn"
if [[ ! -r "$DSN_FILE" ]]; then
    echo "fatal: DSN credential not readable at $DSN_FILE" >&2
    exit 1
fi

exec /usr/local/bin/pgweb \
    --url "$(cat "$DSN_FILE")" \
    --bind 127.0.0.1 \
    --listen 8081 \
    --readonly \
    --skip-open \
    --sessions

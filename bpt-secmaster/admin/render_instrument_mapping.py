#!/usr/bin/env python3
"""
Render the current secmaster state into the legacy instrument_mapping.json
shape that bpt-refdata's InstrumentMappingLoader still reads.

This is the **Path A** sidecar from the phase-6 cutover discussion: lets
the existing bpt-refdata (no C++ changes yet) consume secmaster-sourced
data via the same file path it already knows. Validates the data flow
end-to-end before committing to the C++ PostgresSecmasterSource work.

Run:
    SECMASTER_DSN=postgres://... python render_instrument_mapping.py > mapping.json
    # or fetch from Secrets Manager:
    aws secretsmanager get-secret-value --secret-id bpt-secmaster/db \
        --query SecretString --output text \
      | jq -r .dsn \
      | xargs -I{} env SECMASTER_DSN={} python render_instrument_mapping.py \
      > /opt/bpt/data/instrument_mapping.json

Output JSON shape (matches bpt-refdata/src/mapping/instrument_mapping_loader.cpp):

    {
      "forward": {
        "1_BTCUSDT": <canonical_id>,            # Binance PERP
        "1_BTCUSDT_SPOT": <canonical_id>,       # Binance SPOT (suffixed)
        "2_BTC-USDT-SWAP": <canonical_id>,      # OKX PERP (no suffix needed)
        "3_BTC": <canonical_id>,                # HL PERP
        ...
      },
      "reverse": {
        "<canonical_id>": {
          "base": "BTC", "quote": "USDT", "type": "PERP",
          "exchanges": {"1": "BTCUSDT", "2": "BTC-USDT-SWAP", "3": "BTC"}
        },
        ...
      }
    }

Class mapping (secmaster → legacy `type`):
    spot          → SPOT
    linear-perp   → PERP
    inverse-perp  → PERP   (legacy doesn't distinguish; flag in code below)
    future        → FUTURE
    option        → OPTION (legacy didn't have these; included for completeness)
    index         → skipped (not tradable)

Caveat — canonical IDs are FRESH from secmaster (auto-increment integers
starting at 1), not the legacy hand-allocated ranges (1001-1999 PERP etc.).
Any strategy config referencing a specific canonical_id by number will
break. This is the deliberate trade you accepted when picking "Fresh IDs"
on the cutover plan — re-derive strategy configs from canonical_symbols
instead.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from collections import defaultdict
from typing import Any

import psycopg
from psycopg.rows import dict_row


# Map secmaster's class to the legacy `type` string.
# `None` means skip this row entirely (e.g. indexes aren't tradable).
CLASS_TO_LEGACY_TYPE = {
    "spot": "SPOT",
    "linear-perp": "PERP",
    "inverse-perp": "PERP",
    "future": "FUTURE",
    "option": "OPTION",
    "index": None,
}

# Binance reuses the same `symbol` string across spot and perp surfaces
# (BTCUSDT means both). The legacy loader disambiguates by suffixing
# `_SPOT` on the forward-key for spot listings. Other venues use distinct
# native symbols (OKX: BTC-USDT vs BTC-USDT-SWAP) so no suffix needed.
BINANCE_EXCHANGE_ID = 1


def main() -> int:
    args = _parse_args()
    dsn = os.environ.get("SECMASTER_DSN")
    if not dsn:
        print("error: set SECMASTER_DSN env var", file=sys.stderr)
        return 1

    rows = _fetch_rows(dsn)
    if args.exchange:
        rows = [r for r in rows if r["exchange_id"] == args.exchange]

    forward, reverse = _build_maps(rows)

    output = {"forward": forward, "reverse": reverse}

    if args.output == "-":
        json.dump(output, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        # Atomic write — render to .tmp, fsync, rename. Prevents
        # bpt-refdata from reading a half-written file if it reloads
        # mid-render.
        tmp = args.output + ".tmp"
        with open(tmp, "w") as f:
            json.dump(output, f, indent=2, sort_keys=True)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, args.output)

    print(
        f"rendered {len(forward)} forward keys, "
        f"{len(reverse)} canonical instruments "
        f"(from {len(rows)} listing rows)",
        file=sys.stderr,
    )
    return 0


def _fetch_rows(dsn: str) -> list[dict[str, Any]]:
    """
    Pull current (valid_to IS NULL) listings, joined with the parent
    instrument. Ordered by id so the output is deterministic across runs.
    """
    sql = """
        SELECT i.id              AS canonical_id,
               i.canonical_symbol,
               i.class           AS sm_class,
               i.base_ccy,
               i.quote_ccy,
               l.exchange_id,
               l.venue_native_symbol
        FROM instrument i
        JOIN listing l ON l.instrument_id = i.id
        WHERE i.valid_to IS NULL
          AND l.valid_to IS NULL
          AND l.status = 'live'
        ORDER BY i.id, l.exchange_id
    """
    with psycopg.connect(dsn, row_factory=dict_row) as conn:
        with conn.cursor() as cur:
            cur.execute(sql)
            return list(cur.fetchall())


def _build_maps(rows: list[dict[str, Any]]) -> tuple[dict, dict]:
    forward: dict[str, int] = {}
    reverse: dict[str, dict] = defaultdict(
        lambda: {"base": "", "quote": "", "type": "", "exchanges": {}}
    )

    for r in rows:
        legacy_type = CLASS_TO_LEGACY_TYPE.get(r["sm_class"])
        if legacy_type is None:
            continue

        cid = r["canonical_id"]
        eid = r["exchange_id"]
        sym = r["venue_native_symbol"]

        # Binance-SPOT collision workaround per legacy loader docs.
        key = f"{eid}_{sym}"
        if eid == BINANCE_EXCHANGE_ID and legacy_type == "SPOT":
            key = f"{eid}_{sym}_SPOT"

        forward[key] = cid

        # JSON object keys must be strings; the loader stoul's them back.
        entry = reverse[str(cid)]
        entry["base"] = r["base_ccy"]
        entry["quote"] = r["quote_ccy"]
        entry["type"] = legacy_type
        entry["exchanges"][str(eid)] = sym

    return forward, dict(reverse)


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    p.add_argument(
        "-o",
        "--output",
        default="-",
        help="Output path. '-' (default) writes to stdout. Otherwise atomic-renames.",
    )
    p.add_argument(
        "--exchange",
        type=int,
        help="Filter to a single exchange id (1=Binance, 2=OKX, 3=HL, 4=Deribit).",
    )
    return p.parse_args()


if __name__ == "__main__":
    sys.exit(main())

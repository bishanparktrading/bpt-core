"""
Postgres write helpers with SCD-2 upsert semantics.

The refresher calls these per normalized instrument; each call is
idempotent — re-running the same input produces no changes.

SCD-2 update logic:
  1. SELECT current row WHERE natural_key = ? AND valid_to IS NULL
  2. If not found        → INSERT new row (valid_from=now, valid_to=NULL)
  3. If found + no diff  → no-op
  4. If found + diff     → UPDATE old row SET valid_to=now;
                           INSERT new row (valid_from=now, valid_to=NULL)

Wrapped in a transaction per ingest run so a partial failure rolls back
cleanly. The ingest_run table records the outcome regardless.
"""

from __future__ import annotations

import contextlib
import json
import logging
from dataclasses import dataclass
from datetime import date
from decimal import Decimal
from typing import Iterable, Optional

import psycopg
from psycopg.rows import dict_row

log = logging.getLogger(__name__)


# ────────────────────────────── data types ────────────────────────────


@dataclass(frozen=True)
class NormalizedInstrument:
    """
    One row produced by a venue ingester. Combines the instrument-level
    fields (the canonical_symbol identity) with the listing-level fields
    (venue-specific tick/lot/native symbol). The refresher splits these
    apart at upsert time.
    """

    # ── instrument fields ─────────────────────────────────────────────
    canonical_symbol: str
    class_: str
    base_ccy: str
    quote_ccy: str
    settle_ccy: str
    multiplier: Decimal
    expiry: Optional[date]
    strike: Optional[Decimal]
    putcall: Optional[str]

    # ── listing fields ────────────────────────────────────────────────
    venue_native_symbol: str
    tick_size: Decimal
    lot_size: Decimal
    min_qty: Optional[Decimal]
    min_notional: Optional[Decimal]
    maker_bps: Optional[Decimal]
    taker_bps: Optional[Decimal]
    listed_at: Optional[date]
    status: str  # 'live' | 'suspended' | 'delisted'


@dataclass
class IngestStats:
    rows_added: int = 0
    rows_modified: int = 0
    rows_unchanged: int = 0
    error_count: int = 0
    errors: list[str] = None  # type: ignore[assignment]

    def __post_init__(self) -> None:
        if self.errors is None:
            self.errors = []


# ─────────────────────────── connection helper ────────────────────────


def connect(dsn: str) -> psycopg.Connection:
    """Open a Postgres connection with sensible defaults."""
    return psycopg.connect(
        dsn,
        application_name="bpt-secmaster-refresh",
        # autocommit=False so we get explicit transaction control.
        autocommit=False,
        row_factory=dict_row,
    )


# ─────────────────────────── upsert: instrument ───────────────────────


INSTRUMENT_DIFF_FIELDS = (
    "class",
    "base_ccy",
    "quote_ccy",
    "settle_ccy",
    "multiplier",
    "expiry",
    "strike",
    "putcall",
)


def upsert_instrument(
    cur: psycopg.Cursor,
    inst: NormalizedInstrument,
    change_source: str,
) -> tuple[int, str]:
    """
    Find-or-create-or-SCD2 an instrument row.

    Returns (instrument_id, outcome) where outcome ∈ {'added',
    'modified', 'unchanged'}.
    """
    cur.execute(
        """
        SELECT id, class, base_ccy, quote_ccy, settle_ccy,
               multiplier, expiry, strike, putcall
        FROM instrument
        WHERE canonical_symbol = %s AND valid_to IS NULL
        FOR UPDATE
        """,
        (inst.canonical_symbol,),
    )
    current = cur.fetchone()

    fields_now = {
        "class": inst.class_,
        "base_ccy": inst.base_ccy.upper(),
        "quote_ccy": inst.quote_ccy.upper(),
        "settle_ccy": inst.settle_ccy.upper(),
        "multiplier": inst.multiplier,
        "expiry": inst.expiry,
        "strike": inst.strike,
        "putcall": inst.putcall,
    }

    if current is None:
        # New instrument — INSERT.
        cur.execute(
            """
            INSERT INTO instrument
                (canonical_symbol, class, base_ccy, quote_ccy, settle_ccy,
                 multiplier, expiry, strike, putcall,
                 valid_from, changed_by, change_source)
                VALUES
                (%s, %s, %s, %s, %s, %s, %s, %s, %s,
                 now(), 'system', %s)
            RETURNING id
            """,
            (
                inst.canonical_symbol,
                inst.class_,
                fields_now["base_ccy"],
                fields_now["quote_ccy"],
                fields_now["settle_ccy"],
                inst.multiplier,
                inst.expiry,
                inst.strike,
                inst.putcall,
                change_source,
            ),
        )
        new_id = cur.fetchone()["id"]
        return new_id, "added"

    if _fields_equal(current, fields_now, INSTRUMENT_DIFF_FIELDS):
        return current["id"], "unchanged"

    # SCD-2: close old row, insert new row (preserves the same canonical_symbol).
    # internal_id is allocated fresh — see note below.
    cur.execute(
        "UPDATE instrument SET valid_to = now() WHERE id = %s",
        (current["id"],),
    )
    # NOTE: A new SCD-2 row gets a *new* primary key id. This is a
    # deliberate trade. Pros: simpler schema, clean partial-unique
    # index. Cons: downstream systems that hold instrument_id need to
    # be aware that "current id for this canonical_symbol" can change.
    # In practice they re-load at restart, so the change is invisible
    # except across long-running sessions — and we restart strategies
    # daily anyway.
    #
    # If this becomes a problem later (e.g. long-lived backtests that
    # need stable ids across SCD updates), the fix is a separate
    # `instrument_identity` table with a stable id, and `instrument`
    # becomes the SCD-2 attribute table referencing it. Not worth the
    # extra complexity at v1.
    cur.execute(
        """
        INSERT INTO instrument
            (canonical_symbol, class, base_ccy, quote_ccy, settle_ccy,
             multiplier, expiry, strike, putcall,
             valid_from, changed_by, change_source)
            VALUES
            (%s, %s, %s, %s, %s, %s, %s, %s, %s,
             now(), 'system', %s)
        RETURNING id
        """,
        (
            inst.canonical_symbol,
            inst.class_,
            fields_now["base_ccy"],
            fields_now["quote_ccy"],
            fields_now["settle_ccy"],
            inst.multiplier,
            inst.expiry,
            inst.strike,
            inst.putcall,
            change_source,
        ),
    )
    new_id = cur.fetchone()["id"]
    return new_id, "modified"


# ─────────────────────────── upsert: listing ──────────────────────────


LISTING_DIFF_FIELDS = (
    "venue_native_symbol",
    "tick_size",
    "lot_size",
    "min_qty",
    "min_notional",
    "maker_bps",
    "taker_bps",
    "status",
)


def upsert_listing(
    cur: psycopg.Cursor,
    instrument_id: int,
    exchange_id: int,
    inst: NormalizedInstrument,
    change_source: str,
) -> tuple[int, str]:
    """
    Find-or-create-or-SCD2 a listing row. Returns (listing_id, outcome).
    """
    cur.execute(
        """
        SELECT id, venue_native_symbol, tick_size, lot_size,
               min_qty, min_notional, maker_bps, taker_bps, status
        FROM listing
        WHERE instrument_id = %s AND exchange_id = %s AND valid_to IS NULL
        FOR UPDATE
        """,
        (instrument_id, exchange_id),
    )
    current = cur.fetchone()

    fields_now = {
        "venue_native_symbol": inst.venue_native_symbol,
        "tick_size": inst.tick_size,
        "lot_size": inst.lot_size,
        "min_qty": inst.min_qty,
        "min_notional": inst.min_notional,
        "maker_bps": inst.maker_bps,
        "taker_bps": inst.taker_bps,
        "status": inst.status,
    }

    if current is None:
        cur.execute(
            """
            INSERT INTO listing
                (instrument_id, exchange_id, venue_native_symbol,
                 tick_size, lot_size, min_qty, min_notional,
                 maker_bps, taker_bps, listed_at, status,
                 valid_from, changed_by, change_source)
                VALUES
                (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s,
                 now(), 'system', %s)
            RETURNING id
            """,
            (
                instrument_id,
                exchange_id,
                inst.venue_native_symbol,
                inst.tick_size,
                inst.lot_size,
                inst.min_qty,
                inst.min_notional,
                inst.maker_bps,
                inst.taker_bps,
                inst.listed_at,
                inst.status,
                change_source,
            ),
        )
        new_id = cur.fetchone()["id"]
        return new_id, "added"

    if _fields_equal(current, fields_now, LISTING_DIFF_FIELDS):
        return current["id"], "unchanged"

    cur.execute(
        "UPDATE listing SET valid_to = now() WHERE id = %s",
        (current["id"],),
    )
    cur.execute(
        """
        INSERT INTO listing
            (instrument_id, exchange_id, venue_native_symbol,
             tick_size, lot_size, min_qty, min_notional,
             maker_bps, taker_bps, listed_at, status,
             valid_from, changed_by, change_source)
            VALUES
            (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s,
             now(), 'system', %s)
        RETURNING id
        """,
        (
            instrument_id,
            exchange_id,
            inst.venue_native_symbol,
            inst.tick_size,
            inst.lot_size,
            inst.min_qty,
            inst.min_notional,
            inst.maker_bps,
            inst.taker_bps,
            inst.listed_at,
            inst.status,
            change_source,
        ),
    )
    new_id = cur.fetchone()["id"]
    return new_id, "modified"


# ─────────────────────────── upsert: symbology ────────────────────────


def upsert_symbology(
    cur: psycopg.Cursor,
    instrument_id: int,
    vendor: str,
    value: str,
) -> None:
    """
    Idempotent insert of an external identifier mapping. The PRIMARY KEY
    (instrument_id, vendor, value) makes this a no-op if the row exists.
    """
    cur.execute(
        """
        INSERT INTO symbology (instrument_id, vendor, value, valid_from)
        VALUES (%s, %s, %s, now())
        ON CONFLICT (instrument_id, vendor, value) DO NOTHING
        """,
        (instrument_id, vendor, value),
    )


# ─────────────────────────────── events ───────────────────────────────


def record_event(
    cur: psycopg.Cursor,
    *,
    event_type: str,
    source: str,
    instrument_id: Optional[int] = None,
    listing_id: Optional[int] = None,
    old_value: Optional[dict] = None,
    new_value: Optional[dict] = None,
    description: Optional[str] = None,
) -> None:
    cur.execute(
        """
        INSERT INTO event
            (instrument_id, listing_id, event_type, event_at,
             old_value, new_value, source, description)
            VALUES (%s, %s, %s, now(), %s, %s, %s, %s)
        """,
        (
            instrument_id,
            listing_id,
            event_type,
            json.dumps(old_value) if old_value is not None else None,
            json.dumps(new_value) if new_value is not None else None,
            source,
            description,
        ),
    )


# ─────────────────────────── ingest_run lifecycle ─────────────────────


def start_ingest_run(cur: psycopg.Cursor, source: str) -> int:
    cur.execute(
        """
        INSERT INTO ingest_run (source, started_at, status)
        VALUES (%s, now(), 'running')
        RETURNING id
        """,
        (source,),
    )
    return cur.fetchone()["id"]


def finish_ingest_run(
    cur: psycopg.Cursor,
    run_id: int,
    stats: IngestStats,
    status: str,  # 'ok' | 'partial' | 'failed'
) -> None:
    cur.execute(
        """
        UPDATE ingest_run
        SET finished_at = now(),
            status = %s,
            rows_added = %s,
            rows_modified = %s,
            rows_unchanged = %s,
            error_count = %s,
            notes = %s
        WHERE id = %s
        """,
        (
            status,
            stats.rows_added,
            stats.rows_modified,
            stats.rows_unchanged,
            stats.error_count,
            ("\n".join(stats.errors[:50]) if stats.errors else None),
            run_id,
        ),
    )


# ──────────────────────────── exchange lookup ─────────────────────────


def exchange_id_by_code(cur: psycopg.Cursor, code: str) -> int:
    cur.execute("SELECT id FROM exchange WHERE code = %s", (code,))
    row = cur.fetchone()
    if row is None:
        raise LookupError(f"unknown exchange code: {code}")
    return row["id"]


# ─────────────────────────── helpers (private) ────────────────────────


def _fields_equal(current: dict, fields_now: dict, keys: Iterable[str]) -> bool:
    """
    Compare a DB row against an in-memory dict on the listed keys.
    Coerces Decimal/None equivalently — the venue might omit a field
    (None) while the DB stored NULL; treat both as equal.
    """
    for k in keys:
        cur_v = current.get(k)
        new_v = fields_now.get(k)
        if cur_v is None and new_v is None:
            continue
        if cur_v is None or new_v is None:
            return False
        # Decimal comparison is exact; str fields are case-sensitive.
        if cur_v != new_v:
            return False
    return True


@contextlib.contextmanager
def transaction(conn: psycopg.Connection):
    """
    Single transaction per venue ingest. Commits on success, rolls back
    on exception. The ingest_run row is written inside the transaction —
    on rollback it disappears too, so 'no row' means 'never ran or
    crashed before recording.'
    """
    try:
        with conn.cursor() as cur:
            yield cur
        conn.commit()
    except Exception:
        conn.rollback()
        raise

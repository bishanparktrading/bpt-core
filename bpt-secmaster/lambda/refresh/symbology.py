"""
Canonical symbol derivation — the dedup key across venues.

Grammar (CCXT-extended, venue-agnostic, uppercase):
    spot           BTC/USDT
    linear perp    BTC/USDT:PERPETUAL
    inverse perp   BTC/USD:PERPETUAL.INVERSE
    dated future   BTC/USDT:20251226
    option         BTC/USDT:20251226-90000-C
    index          BTC/USD.INDEX

Two instruments on different venues with the same canonical_symbol share
one internal_id. That's the dedup contract this module enforces.

Pure functions only — no I/O, no logging, no DB. Unit-testable in
isolation.
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import date
from decimal import Decimal
from typing import Optional


# ─── canonical instrument classes (matches schema CHECK constraint) ───
SPOT = "spot"
LINEAR_PERP = "linear-perp"
INVERSE_PERP = "inverse-perp"
FUTURE = "future"
OPTION = "option"
INDEX = "index"

VALID_CLASSES = frozenset({SPOT, LINEAR_PERP, INVERSE_PERP, FUTURE, OPTION, INDEX})


@dataclass(frozen=True)
class CanonicalKey:
    """
    The minimum set of fields needed to derive a canonical symbol.
    Venue ingesters normalize their per-venue metadata into this shape
    before calling `derive_canonical`.
    """

    class_: str  # one of VALID_CLASSES
    base_ccy: str
    quote_ccy: str
    settle_ccy: str
    # Derivatives only.
    expiry: Optional[date] = None
    strike: Optional[Decimal] = None
    putcall: Optional[str] = None  # 'C' or 'P'


def derive_canonical(key: CanonicalKey) -> str:
    """
    Build the canonical symbol from a normalized key.

    Validates the input enough to catch obvious bugs (unknown class,
    missing strike on option, etc.) but trusts the venue ingester to
    have already validated venue-specific quirks.
    """
    if key.class_ not in VALID_CLASSES:
        raise ValueError(f"unknown class: {key.class_!r}")

    base = key.base_ccy.upper()
    quote = key.quote_ccy.upper()
    settle = key.settle_ccy.upper()

    pair = f"{base}/{quote}"

    if key.class_ == SPOT:
        return pair

    if key.class_ == LINEAR_PERP:
        # Inverse perps have settle_ccy == base_ccy. If a venue marks
        # something as linear-perp but settle == base, the ingester
        # has a bug — fail loud rather than silently mis-classifying.
        if settle == base:
            raise ValueError(
                f"linear-perp must not settle in base ({base}); "
                f"caller likely meant {INVERSE_PERP}"
            )
        return f"{pair}:PERPETUAL"

    if key.class_ == INVERSE_PERP:
        if settle != base:
            raise ValueError(
                f"inverse-perp must settle in base ({base}), got settle={settle}"
            )
        return f"{pair}:PERPETUAL.INVERSE"

    if key.class_ == FUTURE:
        if key.expiry is None:
            raise ValueError("future requires expiry")
        return f"{pair}:{key.expiry.strftime('%Y%m%d')}"

    if key.class_ == OPTION:
        if key.expiry is None or key.strike is None or key.putcall is None:
            raise ValueError("option requires expiry + strike + putcall")
        if key.putcall not in ("C", "P"):
            raise ValueError(f"putcall must be C or P, got {key.putcall!r}")
        # Strip trailing zeros from strike for clean canonical form.
        # 90000.00 → "90000", 0.5 → "0.5".
        strike_str = format_strike(key.strike)
        return f"{pair}:{key.expiry.strftime('%Y%m%d')}-{strike_str}-{key.putcall}"

    if key.class_ == INDEX:
        return f"{pair}.INDEX"

    raise AssertionError(f"unreachable: {key.class_}")


def format_strike(strike: Decimal) -> str:
    """
    Format a strike for canonical symbol embedding.

    Strips trailing zeros to keep canonical symbols stable across
    venue-reporting differences. 90000.0 / 90000.00 / 90000 → "90000".
    Fractional strikes (e.g. options on cheap tokens): 0.5 → "0.5".
    """
    # Normalize removes trailing zeros via Decimal's reduce().
    normalized = strike.normalize()
    # But normalize() can produce exponential form for whole numbers:
    # Decimal('90000').normalize() == Decimal('9E+4'). Fix that by
    # formatting with 'f' and stripping any trailing '.0'.
    s = f"{normalized:f}"
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    return s


# ─── helpers for parsing venue-specific epoch / expiry timestamps ─────


def expiry_from_ms(ms_epoch: int) -> date:
    """Convert a venue's millisecond expiry timestamp to a UTC date."""
    from datetime import datetime, timezone

    return datetime.fromtimestamp(ms_epoch / 1000, tz=timezone.utc).date()

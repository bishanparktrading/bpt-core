"""
Deribit instrument ingester.

Source: GET https://www.deribit.com/api/v2/public/get_instruments?currency={BTC,ETH,...}
Docs:   https://docs.deribit.com/#public-get_instruments

Deribit splits its universe by currency. We iterate BTC, ETH, SOL,
XRP (the major underlyings as of 2026); the per-currency response
includes spot/perp/future/option in one list, distinguished by `kind`.

Deribit instrument naming is structured: e.g. `BTC-PERPETUAL`,
`BTC-29DEC25`, `BTC-29DEC25-90000-C`. We parse it loosely but rely
on the structured `kind`/`expiration_timestamp`/`strike`/`option_type`
fields rather than the name itself.
"""

from __future__ import annotations

import logging
from datetime import datetime, timezone
from decimal import Decimal
from typing import Iterable

import symbology
from db import NormalizedInstrument
from venues.base import VenueIngester

log = logging.getLogger(__name__)

DERIBIT_REST_BASE = "https://www.deribit.com"
DERIBIT_CURRENCIES = ("BTC", "ETH", "SOL", "XRP")


class DeribitIngester(VenueIngester):
    exchange_code = "deribit"

    def fetch(self) -> Iterable[NormalizedInstrument]:
        for currency in DERIBIT_CURRENCIES:
            for kind in ("spot", "future", "option"):
                try:
                    body = self._fetch(currency, kind)
                except Exception as e:
                    log.warning("Deribit fetch failed (%s/%s): %s", currency, kind, e)
                    continue
                for row in body.get("result", []):
                    try:
                        norm = self._normalize(row)
                        if norm is not None:
                            yield norm
                    except Exception as e:
                        log.warning(
                            "Deribit: skipping %s: %s",
                            row.get("instrument_name"),
                            e,
                        )

    # ─────────────────────────── HTTP fetch ──────────────────────────

    def _fetch(self, currency: str, kind: str) -> dict:
        url = f"{DERIBIT_REST_BASE}/api/v2/public/get_instruments"
        r = self.http.get(
            url,
            params={"currency": currency, "kind": kind, "expired": "false"},
            timeout=30.0,
        )
        r.raise_for_status()
        return r.json()

    # ──────────────────────────── normalize ──────────────────────────

    def _normalize(self, row: dict) -> NormalizedInstrument | None:
        if not row.get("is_active", True):
            return None

        name = row["instrument_name"]
        kind = row.get("kind", "")
        base = row.get("base_currency", "").upper()
        quote = row.get("quote_currency", "").upper()
        settle = row.get("settlement_currency", base).upper()

        tick = Decimal(str(row["tick_size"]))
        # Deribit reports lot via `contract_size` (face value) +
        # `min_trade_amount`. The two differ subtly; we treat
        # min_trade_amount as the lot for canonical purposes.
        lot = Decimal(str(row["min_trade_amount"]))
        multiplier = Decimal(str(row.get("contract_size") or 1))

        if kind == "spot":
            key = symbology.CanonicalKey(
                class_=symbology.SPOT,
                base_ccy=base,
                quote_ccy=quote,
                settle_ccy=quote,
            )
            return NormalizedInstrument(
                canonical_symbol=symbology.derive_canonical(key),
                class_=symbology.SPOT,
                base_ccy=base,
                quote_ccy=quote,
                settle_ccy=quote,
                multiplier=Decimal(1),
                expiry=None,
                strike=None,
                putcall=None,
                venue_native_symbol=name,
                tick_size=tick,
                lot_size=lot,
                min_qty=lot,
                min_notional=None,
                maker_bps=None,
                taker_bps=None,
                listed_at=None,
                status="live",
            )

        if kind == "future":
            # Deribit "future" includes perpetuals (settlement_period='perpetual')
            # and dated futures.
            settlement_period = row.get("settlement_period", "")
            if settlement_period == "perpetual":
                # Deribit perps are inverse — settle in base currency.
                class_ = symbology.INVERSE_PERP if settle == base else symbology.LINEAR_PERP
                key = symbology.CanonicalKey(
                    class_=class_,
                    base_ccy=base,
                    quote_ccy=quote,
                    settle_ccy=settle,
                )
                return NormalizedInstrument(
                    canonical_symbol=symbology.derive_canonical(key),
                    class_=class_,
                    base_ccy=base,
                    quote_ccy=quote,
                    settle_ccy=settle,
                    multiplier=multiplier,
                    expiry=None,
                    strike=None,
                    putcall=None,
                    venue_native_symbol=name,
                    tick_size=tick,
                    lot_size=lot,
                    min_qty=lot,
                    min_notional=None,
                    maker_bps=None,
                    taker_bps=None,
                    listed_at=None,
                    status="live",
                )
            # Dated future.
            exp_ms = row.get("expiration_timestamp")
            if not exp_ms:
                return None
            expiry = datetime.fromtimestamp(int(exp_ms) / 1000, tz=timezone.utc).date()
            key = symbology.CanonicalKey(
                class_=symbology.FUTURE,
                base_ccy=base,
                quote_ccy=quote,
                settle_ccy=settle,
                expiry=expiry,
            )
            return NormalizedInstrument(
                canonical_symbol=symbology.derive_canonical(key),
                class_=symbology.FUTURE,
                base_ccy=base,
                quote_ccy=quote,
                settle_ccy=settle,
                multiplier=multiplier,
                expiry=expiry,
                strike=None,
                putcall=None,
                venue_native_symbol=name,
                tick_size=tick,
                lot_size=lot,
                min_qty=lot,
                min_notional=None,
                maker_bps=None,
                taker_bps=None,
                listed_at=None,
                status="live",
            )

        if kind == "option":
            exp_ms = row.get("expiration_timestamp")
            strike = row.get("strike")
            opt_type = row.get("option_type", "")  # 'call' | 'put'
            if not exp_ms or strike is None or opt_type not in ("call", "put"):
                return None
            expiry = datetime.fromtimestamp(int(exp_ms) / 1000, tz=timezone.utc).date()
            putcall = "C" if opt_type == "call" else "P"
            # Deribit reports quote_currency=BTC for BTC options (they're
            # BTC-denominated for premium), but the STRIKE is in USD. For
            # our canonical_symbol we want the strike currency as the
            # quote slot — use counter_currency if present, fall back to
            # USD which is correct for every Deribit option listed today.
            opt_quote = row.get("counter_currency", "USD").upper() or "USD"
            key = symbology.CanonicalKey(
                class_=symbology.OPTION,
                base_ccy=base,
                quote_ccy=opt_quote,
                settle_ccy=settle,
                expiry=expiry,
                strike=Decimal(str(strike)),
                putcall=putcall,
            )
            return NormalizedInstrument(
                canonical_symbol=symbology.derive_canonical(key),
                class_=symbology.OPTION,
                base_ccy=base,
                quote_ccy=opt_quote,
                settle_ccy=settle,
                multiplier=multiplier,
                expiry=expiry,
                strike=Decimal(str(strike)),
                putcall=putcall,
                venue_native_symbol=name,
                tick_size=tick,
                lot_size=lot,
                min_qty=lot,
                min_notional=None,
                maker_bps=None,
                taker_bps=None,
                listed_at=None,
                status="live",
            )

        return None

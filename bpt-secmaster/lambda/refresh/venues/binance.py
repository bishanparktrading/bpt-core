"""
Binance instrument ingester.

Three endpoints (Binance splits its product surface into three APIs):
  Spot:        GET https://api.binance.com/api/v3/exchangeInfo
  USDT-M perp: GET https://fapi.binance.com/fapi/v1/exchangeInfo
  Coin-M perp: GET https://dapi.binance.com/dapi/v1/exchangeInfo  (inverse perps)

The futures endpoints also return quarterly futures alongside perpetuals;
we distinguish via `contractType` ('PERPETUAL' vs 'CURRENT_QUARTER' /
'NEXT_QUARTER').

Tick / lot extraction: filters[] array contains PRICE_FILTER (tickSize),
LOT_SIZE (stepSize, minQty), and MIN_NOTIONAL.
"""

from __future__ import annotations

import logging
import re
from datetime import datetime, timezone
from decimal import Decimal
from typing import Iterable

import symbology
from db import NormalizedInstrument
from venues.base import VenueIngester

log = logging.getLogger(__name__)

BINANCE_SPOT_URL = "https://api.binance.com/api/v3/exchangeInfo"
BINANCE_FUT_USDT_URL = "https://fapi.binance.com/fapi/v1/exchangeInfo"
BINANCE_FUT_COIN_URL = "https://dapi.binance.com/dapi/v1/exchangeInfo"

# Binance futures symbol format: BTCUSDT (perp) | BTCUSDT_250627 (quarterly)
_QUARTERLY_SUFFIX = re.compile(r"_(\d{6})$")


class BinanceIngester(VenueIngester):
    exchange_code = "binance"

    def fetch(self) -> Iterable[NormalizedInstrument]:
        yield from self._fetch_spot()
        yield from self._fetch_usdt_m()
        yield from self._fetch_coin_m()

    # ──────────────────────────── spot ────────────────────────────────

    def _fetch_spot(self) -> Iterable[NormalizedInstrument]:
        body = self.http.get(BINANCE_SPOT_URL, timeout=30.0).json()
        for row in body.get("symbols", []):
            try:
                if row.get("status") != "TRADING":
                    continue
                base = row["baseAsset"].upper()
                quote = row["quoteAsset"].upper()
                filters = _filters(row)
                tick = Decimal(filters["PRICE_FILTER"]["tickSize"])
                lot = Decimal(filters["LOT_SIZE"]["stepSize"])
                min_qty = Decimal(filters["LOT_SIZE"]["minQty"])
                min_notional = (
                    Decimal(filters["NOTIONAL"]["minNotional"])
                    if "NOTIONAL" in filters
                    else None
                )

                key = symbology.CanonicalKey(
                    class_=symbology.SPOT,
                    base_ccy=base,
                    quote_ccy=quote,
                    settle_ccy=quote,
                )

                yield NormalizedInstrument(
                    canonical_symbol=symbology.derive_canonical(key),
                    class_=symbology.SPOT,
                    base_ccy=base,
                    quote_ccy=quote,
                    settle_ccy=quote,
                    multiplier=Decimal(1),
                    expiry=None,
                    strike=None,
                    putcall=None,
                    venue_native_symbol=row["symbol"],
                    tick_size=tick,
                    lot_size=lot,
                    min_qty=min_qty,
                    min_notional=min_notional,
                    maker_bps=None,
                    taker_bps=None,
                    listed_at=None,
                    status="live",
                )
            except Exception as e:
                log.warning("Binance spot: skipping %s: %s", row.get("symbol"), e)

    # ────────────────────────── USDT-M (linear) ───────────────────────

    def _fetch_usdt_m(self) -> Iterable[NormalizedInstrument]:
        body = self.http.get(BINANCE_FUT_USDT_URL, timeout=30.0).json()
        for row in body.get("symbols", []):
            try:
                if row.get("status") != "TRADING":
                    continue
                base = row["baseAsset"].upper()
                quote = row["quoteAsset"].upper()
                settle = row.get("marginAsset", quote).upper()
                contract_type = row.get("contractType", "")

                filters = _filters(row)
                tick = Decimal(filters["PRICE_FILTER"]["tickSize"])
                lot = Decimal(filters["LOT_SIZE"]["stepSize"])
                min_qty = Decimal(filters["LOT_SIZE"]["minQty"])
                min_notional = (
                    Decimal(filters["MIN_NOTIONAL"]["notional"])
                    if "MIN_NOTIONAL" in filters
                    else None
                )

                if contract_type == "PERPETUAL":
                    key = symbology.CanonicalKey(
                        class_=symbology.LINEAR_PERP,
                        base_ccy=base,
                        quote_ccy=quote,
                        settle_ccy=settle,
                    )
                    class_ = symbology.LINEAR_PERP
                    expiry = None
                elif contract_type in ("CURRENT_QUARTER", "NEXT_QUARTER", "CURRENT_QUARTER_DELIVERING"):
                    delivery_ms = row.get("deliveryDate")
                    if not delivery_ms:
                        continue
                    expiry = datetime.fromtimestamp(
                        int(delivery_ms) / 1000, tz=timezone.utc
                    ).date()
                    key = symbology.CanonicalKey(
                        class_=symbology.FUTURE,
                        base_ccy=base,
                        quote_ccy=quote,
                        settle_ccy=settle,
                        expiry=expiry,
                    )
                    class_ = symbology.FUTURE
                else:
                    continue  # skip unknown contract types

                yield NormalizedInstrument(
                    canonical_symbol=symbology.derive_canonical(key),
                    class_=class_,
                    base_ccy=base,
                    quote_ccy=quote,
                    settle_ccy=settle,
                    multiplier=Decimal(1),
                    expiry=expiry,
                    strike=None,
                    putcall=None,
                    venue_native_symbol=row["symbol"],
                    tick_size=tick,
                    lot_size=lot,
                    min_qty=min_qty,
                    min_notional=min_notional,
                    maker_bps=None,
                    taker_bps=None,
                    listed_at=None,
                    status="live",
                )
            except Exception as e:
                log.warning("Binance USDT-M: skipping %s: %s", row.get("symbol"), e)

    # ────────────────────────── Coin-M (inverse) ──────────────────────

    def _fetch_coin_m(self) -> Iterable[NormalizedInstrument]:
        body = self.http.get(BINANCE_FUT_COIN_URL, timeout=30.0).json()
        for row in body.get("symbols", []):
            try:
                if row.get("contractStatus") != "TRADING":
                    continue
                base = row["baseAsset"].upper()
                quote = row["quoteAsset"].upper()
                settle = row.get("marginAsset", base).upper()
                contract_type = row.get("contractType", "")

                filters = _filters(row)
                tick = Decimal(filters["PRICE_FILTER"]["tickSize"])
                lot = Decimal(filters["LOT_SIZE"]["stepSize"])
                min_qty = Decimal(filters["LOT_SIZE"]["minQty"])
                multiplier = Decimal(row.get("contractSize") or "1")

                if contract_type == "PERPETUAL":
                    # Coin-M = inverse perps (settle in BTC for BTCUSD etc.).
                    key = symbology.CanonicalKey(
                        class_=symbology.INVERSE_PERP,
                        base_ccy=base,
                        quote_ccy=quote,
                        settle_ccy=settle,
                    )
                    class_ = symbology.INVERSE_PERP
                    expiry = None
                elif contract_type in ("CURRENT_QUARTER", "NEXT_QUARTER"):
                    delivery_ms = row.get("deliveryDate")
                    if not delivery_ms:
                        continue
                    expiry = datetime.fromtimestamp(
                        int(delivery_ms) / 1000, tz=timezone.utc
                    ).date()
                    key = symbology.CanonicalKey(
                        class_=symbology.FUTURE,
                        base_ccy=base,
                        quote_ccy=quote,
                        settle_ccy=settle,
                        expiry=expiry,
                    )
                    class_ = symbology.FUTURE
                else:
                    continue

                yield NormalizedInstrument(
                    canonical_symbol=symbology.derive_canonical(key),
                    class_=class_,
                    base_ccy=base,
                    quote_ccy=quote,
                    settle_ccy=settle,
                    multiplier=multiplier,
                    expiry=expiry,
                    strike=None,
                    putcall=None,
                    venue_native_symbol=row["symbol"],
                    tick_size=tick,
                    lot_size=lot,
                    min_qty=min_qty,
                    min_notional=None,
                    maker_bps=None,
                    taker_bps=None,
                    listed_at=None,
                    status="live",
                )
            except Exception as e:
                log.warning("Binance coin-M: skipping %s: %s", row.get("symbol"), e)


def _filters(row: dict) -> dict[str, dict]:
    """Flatten Binance's filters[] list into a {filterType: filter_row} dict."""
    return {f["filterType"]: f for f in row.get("filters", [])}

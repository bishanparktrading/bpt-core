"""
Hyperliquid instrument ingester.

Sources:
  POST https://api.hyperliquid.xyz/info {"type": "meta"}     ← perp universe
  POST https://api.hyperliquid.xyz/info {"type": "spotMeta"} ← spot universe

Docs: https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/info-endpoint

HL perps are all USD-quoted, USD-settled, linear, perpetual.
HL spot is structured differently: each spot pair has a token index
and a quote token index; we resolve back to base/quote ccys.

Tick / lot rules on HL:
  - perps: szDecimals controls qty precision; price has its own rules
    (5 significant figures or szDecimals + 6, whichever smaller)
  - For our purposes we approximate tick=10^-pxDecimals, lot=10^-szDecimals.
    Some HL instruments have venue-side overrides; capture what /info
    gives us and accept the small fidelity gap.
"""

from __future__ import annotations

import logging
from decimal import Decimal
from typing import Iterable

import symbology
from db import NormalizedInstrument
from venues.base import VenueIngester

log = logging.getLogger(__name__)

HL_REST_BASE = "https://api.hyperliquid.xyz"

# HL doesn't expose pxDecimals directly; derive from szDecimals.
# Empirically: tick = 1 / 10^(max(0, 6 - szDecimals)) for liquid coins.
# This is approximate; the true rule is "5 sig figs OR szDecimals+6,
# whichever is smaller." For most instruments the simple rule is fine.
_DEFAULT_TICK_PRECISION = 6


class HyperliquidIngester(VenueIngester):
    exchange_code = "hl"

    def fetch(self) -> Iterable[NormalizedInstrument]:
        yield from self._fetch_perps()
        yield from self._fetch_spot()

    # ───────────────────────────── perps ──────────────────────────────

    def _fetch_perps(self) -> Iterable[NormalizedInstrument]:
        body = self._post({"type": "meta"})
        universe = body.get("universe", [])
        for row in universe:
            try:
                norm = self._normalize_perp(row)
                if norm is not None:
                    yield norm
            except Exception as e:
                log.warning("HL: skipping perp %s: %s", row.get("name"), e)

    def _normalize_perp(self, row: dict) -> NormalizedInstrument | None:
        name = row.get("name")
        sz_decimals = row.get("szDecimals")
        if name is None or sz_decimals is None:
            return None
        if row.get("isDelisted"):
            return None  # skip delisted entirely

        # HL perps: <COIN>/USD:PERPETUAL (linear, USD-settled).
        base = name.upper()
        quote = "USD"
        settle = "USD"

        # Approximate tick + lot.
        lot = Decimal(1).scaleb(-int(sz_decimals))
        # Price decimals = max(0, 6 - szDecimals) per HL's rule of thumb.
        px_decimals = max(0, _DEFAULT_TICK_PRECISION - int(sz_decimals))
        tick = Decimal(1).scaleb(-px_decimals)

        key = symbology.CanonicalKey(
            class_=symbology.LINEAR_PERP,
            base_ccy=base,
            quote_ccy=quote,
            settle_ccy=settle,
        )

        return NormalizedInstrument(
            canonical_symbol=symbology.derive_canonical(key),
            class_=symbology.LINEAR_PERP,
            base_ccy=base,
            quote_ccy=quote,
            settle_ccy=settle,
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

    # ───────────────────────────── spot ───────────────────────────────

    def _fetch_spot(self) -> Iterable[NormalizedInstrument]:
        body = self._post({"type": "spotMeta"})
        tokens = body.get("tokens", [])
        # tokens: [{'name': 'BTC', 'szDecimals': 5, 'weiDecimals': 8, 'index': 0, ...}, ...]
        # universe: [{'name': '@1', 'tokens': [base_idx, quote_idx], ...}, ...]
        # HL gives spot pairs as @N synthetic names; we resolve to base/quote.
        token_by_index = {t["index"]: t for t in tokens}
        universe = body.get("universe", [])
        for row in universe:
            try:
                norm = self._normalize_spot(row, token_by_index)
                if norm is not None:
                    yield norm
            except Exception as e:
                log.warning("HL: skipping spot %s: %s", row.get("name"), e)

    def _normalize_spot(
        self, row: dict, token_by_index: dict[int, dict]
    ) -> NormalizedInstrument | None:
        tokens = row.get("tokens", [])
        if len(tokens) != 2:
            return None
        base_token = token_by_index.get(tokens[0])
        quote_token = token_by_index.get(tokens[1])
        if base_token is None or quote_token is None:
            return None

        base = base_token["name"].upper()
        quote = quote_token["name"].upper()
        sz_decimals = int(base_token.get("szDecimals", 0))

        lot = Decimal(1).scaleb(-sz_decimals)
        px_decimals = max(0, _DEFAULT_TICK_PRECISION - sz_decimals)
        tick = Decimal(1).scaleb(-px_decimals)

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
            # HL uses '@N' for spot pairs internally. Keep that as the
            # venue_native so the order adapter can address it directly.
            venue_native_symbol=row["name"],
            tick_size=tick,
            lot_size=lot,
            min_qty=lot,
            min_notional=None,
            maker_bps=None,
            taker_bps=None,
            listed_at=None,
            status="live",
        )

    # ─────────────────────────── HTTP helper ──────────────────────────

    def _post(self, body: dict) -> dict:
        r = self.http.post(f"{HL_REST_BASE}/info", json=body, timeout=30.0)
        r.raise_for_status()
        return r.json()

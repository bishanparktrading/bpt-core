"""
Unit tests for canonical_symbol derivation. This is the single most
load-bearing piece of secmaster — a derivation bug means two venues'
listings dedup to wrong/different internal_ids, which fans out into
broken cross-venue queries forever.

Run: pytest bpt-secmaster/lambda/refresh/tests/test_symbology.py
"""

from datetime import date
from decimal import Decimal

import pytest

from symbology import (
    INDEX,
    INVERSE_PERP,
    LINEAR_PERP,
    OPTION,
    SPOT,
    FUTURE,
    CanonicalKey,
    derive_canonical,
    format_strike,
)


class TestSpot:
    def test_basic(self):
        k = CanonicalKey(SPOT, "BTC", "USDT", "USDT")
        assert derive_canonical(k) == "BTC/USDT"

    def test_lowercase_input_normalized(self):
        k = CanonicalKey(SPOT, "btc", "usdt", "usdt")
        assert derive_canonical(k) == "BTC/USDT"

    def test_nonstandard_quote(self):
        k = CanonicalKey(SPOT, "ETH", "USDC", "USDC")
        assert derive_canonical(k) == "ETH/USDC"


class TestLinearPerp:
    def test_usdt_quoted_settled(self):
        k = CanonicalKey(LINEAR_PERP, "BTC", "USDT", "USDT")
        assert derive_canonical(k) == "BTC/USDT:PERPETUAL"

    def test_usd_quoted_settled(self):
        # HL: BTC/USD:PERPETUAL (USD-quoted linear)
        k = CanonicalKey(LINEAR_PERP, "BTC", "USD", "USD")
        assert derive_canonical(k) == "BTC/USD:PERPETUAL"

    def test_settles_in_base_is_error(self):
        # linear-perp must not settle in base — that's inverse.
        k = CanonicalKey(LINEAR_PERP, "BTC", "USDT", "BTC")
        with pytest.raises(ValueError, match="linear-perp must not settle in base"):
            derive_canonical(k)


class TestInversePerp:
    def test_basic(self):
        # Deribit BTC-PERPETUAL: BTC quote, BTC settle.
        k = CanonicalKey(INVERSE_PERP, "BTC", "USD", "BTC")
        assert derive_canonical(k) == "BTC/USD:PERPETUAL.INVERSE"

    def test_settles_not_in_base_is_error(self):
        k = CanonicalKey(INVERSE_PERP, "BTC", "USD", "USDT")
        with pytest.raises(ValueError, match="inverse-perp must settle in base"):
            derive_canonical(k)


class TestFuture:
    def test_basic(self):
        k = CanonicalKey(FUTURE, "BTC", "USDT", "USDT", expiry=date(2025, 12, 26))
        assert derive_canonical(k) == "BTC/USDT:20251226"

    def test_missing_expiry_is_error(self):
        k = CanonicalKey(FUTURE, "BTC", "USDT", "USDT")
        with pytest.raises(ValueError, match="future requires expiry"):
            derive_canonical(k)


class TestOption:
    def test_call(self):
        k = CanonicalKey(
            OPTION, "BTC", "USDT", "USDT",
            expiry=date(2025, 12, 26),
            strike=Decimal("90000"),
            putcall="C",
        )
        assert derive_canonical(k) == "BTC/USDT:20251226-90000-C"

    def test_put(self):
        k = CanonicalKey(
            OPTION, "BTC", "USDT", "USDT",
            expiry=date(2025, 12, 26),
            strike=Decimal("90000"),
            putcall="P",
        )
        assert derive_canonical(k) == "BTC/USDT:20251226-90000-P"

    def test_fractional_strike(self):
        # Options on a cheap token, e.g. DOGE at $0.15.
        k = CanonicalKey(
            OPTION, "DOGE", "USDT", "USDT",
            expiry=date(2025, 12, 26),
            strike=Decimal("0.15"),
            putcall="C",
        )
        assert derive_canonical(k) == "DOGE/USDT:20251226-0.15-C"

    def test_strike_with_trailing_zeros_normalized(self):
        # Venue might report 90000.00 or 90000.0 or 90000;
        # canonical form should be the same.
        for s in ("90000", "90000.0", "90000.00", "90000.000"):
            k = CanonicalKey(
                OPTION, "BTC", "USDT", "USDT",
                expiry=date(2025, 12, 26),
                strike=Decimal(s),
                putcall="C",
            )
            assert derive_canonical(k) == "BTC/USDT:20251226-90000-C", (
                f"strike form {s!r} did not normalize"
            )

    def test_missing_fields_is_error(self):
        for missing in ("expiry", "strike", "putcall"):
            kwargs = dict(
                class_=OPTION,
                base_ccy="BTC",
                quote_ccy="USDT",
                settle_ccy="USDT",
                expiry=date(2025, 12, 26),
                strike=Decimal("90000"),
                putcall="C",
            )
            kwargs[missing] = None
            k = CanonicalKey(**kwargs)
            with pytest.raises(ValueError):
                derive_canonical(k)

    def test_bad_putcall_rejected(self):
        k = CanonicalKey(
            OPTION, "BTC", "USDT", "USDT",
            expiry=date(2025, 12, 26),
            strike=Decimal("90000"),
            putcall="X",  # invalid
        )
        with pytest.raises(ValueError, match="putcall must be C or P"):
            derive_canonical(k)


class TestIndex:
    def test_basic(self):
        k = CanonicalKey(INDEX, "BTC", "USD", "USD")
        assert derive_canonical(k) == "BTC/USD.INDEX"


class TestUnknownClass:
    def test_rejected(self):
        with pytest.raises(ValueError, match="unknown class"):
            derive_canonical(CanonicalKey("crypto-NFT-thing", "X", "Y", "Z"))


class TestFormatStrike:
    @pytest.mark.parametrize(
        "input_str,expected",
        [
            ("90000", "90000"),
            ("90000.0", "90000"),
            ("90000.00", "90000"),
            ("90000.5", "90000.5"),
            ("0.5", "0.5"),
            ("0.15", "0.15"),
            ("0.001", "0.001"),
        ],
    )
    def test_normalizes(self, input_str, expected):
        assert format_strike(Decimal(input_str)) == expected


class TestCrossVenueDedup:
    """
    Test the contract that two venues with the 'same instrument'
    derive to the same canonical_symbol. Failures here mean the dedup
    breaks downstream.
    """

    def test_btc_perp_same_across_okx_hl_binance(self):
        # All three: linear perp, BTC base, USDT (or USD on HL) settle.
        okx = CanonicalKey(LINEAR_PERP, "BTC", "USDT", "USDT")
        binance = CanonicalKey(LINEAR_PERP, "BTC", "USDT", "USDT")
        assert derive_canonical(okx) == derive_canonical(binance)

    def test_hl_btc_perp_is_distinct_from_okx_btc_perp(self):
        # HL is USD-quoted, OKX is USDT-quoted — different economic
        # exposures, must have DIFFERENT canonical symbols.
        hl = CanonicalKey(LINEAR_PERP, "BTC", "USD", "USD")
        okx = CanonicalKey(LINEAR_PERP, "BTC", "USDT", "USDT")
        assert derive_canonical(hl) != derive_canonical(okx)

    def test_deribit_btc_perpetual_is_inverse(self):
        # Deribit BTC-PERPETUAL settles in BTC → inverse. Must be
        # distinct from the linear flavor.
        deribit = CanonicalKey(INVERSE_PERP, "BTC", "USD", "BTC")
        okx = CanonicalKey(LINEAR_PERP, "BTC", "USDT", "USDT")
        assert derive_canonical(deribit) == "BTC/USD:PERPETUAL.INVERSE"
        assert derive_canonical(deribit) != derive_canonical(okx)

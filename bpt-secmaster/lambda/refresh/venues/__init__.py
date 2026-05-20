from .base import VenueIngester
from .binance import BinanceIngester
from .deribit import DeribitIngester
from .hl import HyperliquidIngester
from .okx import OkxIngester

__all__ = [
    "VenueIngester",
    "BinanceIngester",
    "DeribitIngester",
    "HyperliquidIngester",
    "OkxIngester",
]


def all_ingesters() -> list[type[VenueIngester]]:
    """Used by the Lambda handler to iterate over every venue per run."""
    return [OkxIngester, HyperliquidIngester, BinanceIngester, DeribitIngester]

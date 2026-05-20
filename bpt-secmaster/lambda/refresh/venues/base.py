"""
Abstract base for per-venue instrument ingesters.

A VenueIngester:
  1. Fetches the venue's instrument metadata via its public REST API.
  2. Normalizes each instrument into a NormalizedInstrument.
  3. Yields the normalized list back to the refresher.

It does NOT touch the DB — the refresher owns the upsert loop. This
keeps ingesters pure (input: HTTP, output: dicts) and unit-testable
against captured API fixtures.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Iterable

import httpx

from db import NormalizedInstrument


class VenueIngester(ABC):
    """One per venue. Subclasses set `exchange_code` and implement `fetch`."""

    exchange_code: str  # 'okx' | 'hl' | 'binance' | 'deribit'

    def __init__(self, http_client: httpx.Client) -> None:
        self.http = http_client

    @abstractmethod
    def fetch(self) -> Iterable[NormalizedInstrument]:
        """
        Pull from venue REST API + normalize. Yields one
        NormalizedInstrument per (instrument, listing) pair seen.

        Raises on transport failure (HTTP error, timeout, JSON parse
        failure). Per-row parse failures should be logged + skipped,
        not raised — one bad row shouldn't kill the whole venue.
        """
        ...

"""Reader for .canon files. Wire format in bpt-canon/include/canon/canon_format.h."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import TYPE_CHECKING, BinaryIO, Iterator, Union

if TYPE_CHECKING:
    import pandas as pd

PathLike = Union[str, Path]

# Must match canon_format.h.
MAGIC = b"BPTC"
SCHEMA_VERSION = 1
FILE_HEADER_BYTES = 96
RECORD_HEADER_BYTES = 11
SBE_MESSAGE_HEADER_BYTES = 8  # blockLength u16 + templateId u16 + schemaId u16 + version u16


class EventType(IntEnum):
    BBO = 0
    TRADE = 1
    BOOK = 2
    FUNDING = 3
    MARK = 4


_FILE_HEADER_FMT = "<4sHHBBI16s40s26s"
assert struct.calcsize(_FILE_HEADER_FMT) == FILE_HEADER_BYTES


@dataclass(frozen=True)
class FileHeader:
    schema_version: int
    sbe_template_id: int
    venue_id: int
    flags: int
    date_utc: int  # YYYYMMDD
    producer_kind: str
    producer_sha: str


def read_header(path: PathLike) -> FileHeader:
    with open(path, "rb") as f:
        return _parse_header(f)


def _parse_header(f: BinaryIO) -> FileHeader:
    buf = f.read(FILE_HEADER_BYTES)
    if len(buf) < FILE_HEADER_BYTES:
        raise ValueError(f"truncated canon file: header is {len(buf)} bytes, need {FILE_HEADER_BYTES}")
    magic, version, tid, venue, flags, date, kind, sha, _ = struct.unpack(_FILE_HEADER_FMT, buf)
    if magic != MAGIC:
        raise ValueError(f"not a canon file (magic={magic!r}, expected {MAGIC!r})")
    if version != SCHEMA_VERSION:
        raise ValueError(f"unsupported canon schema_version={version} (reader supports {SCHEMA_VERSION})")
    return FileHeader(
        schema_version=version,
        sbe_template_id=tid,
        venue_id=venue,
        flags=flags,
        date_utc=date,
        producer_kind=kind.rstrip(b"\x00").decode("ascii", errors="replace"),
        producer_sha=sha.rstrip(b"\x00").decode("ascii", errors="replace"),
    )


_RECORD_HEADER_FMT = "<QBH"  # ts_ns u64 + event_t u8 + sbe_len u16
assert struct.calcsize(_RECORD_HEADER_FMT) == RECORD_HEADER_BYTES


@dataclass(frozen=True)
class RawRecord:
    ts_ns: int
    event_type: EventType
    payload: bytes  # full SBE blob incl. 8-byte MessageHeader


def iter_records(path: PathLike) -> Iterator[RawRecord]:
    with open(path, "rb") as f:
        _parse_header(f)
        while True:
            hdr = f.read(RECORD_HEADER_BYTES)
            if not hdr:
                return
            if len(hdr) < RECORD_HEADER_BYTES:
                raise ValueError(f"truncated record header ({len(hdr)} of {RECORD_HEADER_BYTES} bytes)")
            ts_ns, event_t, sbe_len = struct.unpack(_RECORD_HEADER_FMT, hdr)
            payload = f.read(sbe_len)
            if len(payload) < sbe_len:
                raise ValueError(f"truncated record payload ({len(payload)} of {sbe_len} bytes)")
            try:
                et = EventType(event_t)
            except ValueError:
                et = event_t  # type: ignore[assignment]  # forward-compat: unknown enum value
            yield RawRecord(ts_ns=ts_ns, event_type=et, payload=payload)


# MdMarketData (template 7): ts u64 / iid u64 / bidPx f64 / bidQty f64 / askPx f64 / askQty f64 / seq u64
_BBO_FMT = "<QQddddQ"
_BBO_BLOCK_BYTES = 56
assert struct.calcsize(_BBO_FMT) == _BBO_BLOCK_BYTES


def read_bbos(path: PathLike) -> "pd.DataFrame":
    """Columns: ts_ns, instrument_id, bid, bid_qty, ask, ask_qty, seq_num."""
    import pandas as pd

    rows = []
    for rec in iter_records(path):
        if rec.event_type != EventType.BBO:
            continue
        body = rec.payload[SBE_MESSAGE_HEADER_BYTES : SBE_MESSAGE_HEADER_BYTES + _BBO_BLOCK_BYTES]
        _producer_ts, iid, bid, bid_qty, ask, ask_qty, seq = struct.unpack(_BBO_FMT, body)
        rows.append((rec.ts_ns, iid, bid, bid_qty, ask, ask_qty, seq))
    return pd.DataFrame(rows, columns=["ts_ns", "instrument_id", "bid", "bid_qty", "ask", "ask_qty", "seq_num"])


# MdTrade (template 8): ts u64 / iid u64 / px f64 / qty f64 / side u8 / seq u64 (unaligned)
_TRADE_FMT = "<QQddBQ"
_TRADE_BLOCK_BYTES = 41
assert struct.calcsize(_TRADE_FMT) == _TRADE_BLOCK_BYTES


def read_trades(path: PathLike) -> "pd.DataFrame":
    """Columns: ts_ns, instrument_id, price, qty, side, seq_num.

    side: OrderSide byte (0=BUY, 1=SELL, 255=NULL).
    """
    import pandas as pd

    rows = []
    for rec in iter_records(path):
        if rec.event_type != EventType.TRADE:
            continue
        body = rec.payload[SBE_MESSAGE_HEADER_BYTES : SBE_MESSAGE_HEADER_BYTES + _TRADE_BLOCK_BYTES]
        _producer_ts, iid, price, qty, side, seq = struct.unpack(_TRADE_FMT, body)
        rows.append((rec.ts_ns, iid, price, qty, side, seq))
    return pd.DataFrame(rows, columns=["ts_ns", "instrument_id", "price", "qty", "side", "seq_num"])

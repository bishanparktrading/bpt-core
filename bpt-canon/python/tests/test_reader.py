"""Synth-file round-trip: header + iter_records. DataFrame wrappers not covered (thin)."""

import struct
import tempfile
import unittest
from pathlib import Path

from bpt_canon import EventType, iter_records, read_header
from bpt_canon.reader import (
    FILE_HEADER_BYTES,
    MAGIC,
    RECORD_HEADER_BYTES,
    SBE_MESSAGE_HEADER_BYTES,
    SCHEMA_VERSION,
    _BBO_BLOCK_BYTES,
    _BBO_FMT,
    _TRADE_BLOCK_BYTES,
    _TRADE_FMT,
)

_FILE_HEADER_FMT = "<4sHHBBI16s40s26s"

# SBE template IDs (must match the generated headers we decode against).
BBO_TEMPLATE_ID = 7
TRADE_TEMPLATE_ID = 8
SBE_SCHEMA_ID = 1
SBE_SCHEMA_VERSION = 0


def _pack_sbe_msg_header(block_length: int, template_id: int) -> bytes:
    return struct.pack("<HHHH", block_length, template_id, SBE_SCHEMA_ID, SBE_SCHEMA_VERSION)


def _make_canon_file(path: Path) -> None:
    """Write a 3-record canon file: BBO, TRADE, BBO."""
    with open(path, "wb") as f:
        # File header
        header = struct.pack(
            _FILE_HEADER_FMT,
            MAGIC,
            SCHEMA_VERSION,
            BBO_TEMPLATE_ID,  # primary template id (informational)
            42,  # venue_id
            0,  # flags
            20260522,  # date_utc
            b"test-producer\x00\x00\x00",
            b"deadbeef" * 5,
            b"\x00" * 26,
        )
        assert len(header) == FILE_HEADER_BYTES
        f.write(header)

        # Record 1: BBO @ ts=1_000_000_000
        body1 = struct.pack(
            _BBO_FMT,
            1_000_000_000,  # producer ts (ignored by reader)
            12345,  # instrument_id
            100.0,  # bid
            5.0,  # bid_qty
            100.1,  # ask
            3.0,  # ask_qty
            1,  # seq_num
        )
        sbe1 = _pack_sbe_msg_header(_BBO_BLOCK_BYTES, BBO_TEMPLATE_ID) + body1
        rec1 = struct.pack("<QBH", 1_000_000_000, EventType.BBO, len(sbe1)) + sbe1
        f.write(rec1)

        # Record 2: TRADE @ ts=1_500_000_000
        body2 = struct.pack(
            _TRADE_FMT,
            1_500_000_000,
            12345,
            100.05,
            2.0,
            0,  # BUY taker
            2,
        )
        sbe2 = _pack_sbe_msg_header(_TRADE_BLOCK_BYTES, TRADE_TEMPLATE_ID) + body2
        rec2 = struct.pack("<QBH", 1_500_000_000, EventType.TRADE, len(sbe2)) + sbe2
        f.write(rec2)

        # Record 3: BBO @ ts=2_000_000_000
        body3 = struct.pack(
            _BBO_FMT,
            2_000_000_000,
            12345,
            100.05,
            4.0,
            100.15,
            6.0,
            3,
        )
        sbe3 = _pack_sbe_msg_header(_BBO_BLOCK_BYTES, BBO_TEMPLATE_ID) + body3
        rec3 = struct.pack("<QBH", 2_000_000_000, EventType.BBO, len(sbe3)) + sbe3
        f.write(rec3)


class TestCanonReader(unittest.TestCase):
    def setUp(self) -> None:
        self._tmpdir = tempfile.TemporaryDirectory()
        self.path = Path(self._tmpdir.name) / "test.canon"
        _make_canon_file(self.path)

    def tearDown(self) -> None:
        self._tmpdir.cleanup()

    def test_header_parses(self):
        h = read_header(self.path)
        self.assertEqual(h.schema_version, SCHEMA_VERSION)
        self.assertEqual(h.sbe_template_id, BBO_TEMPLATE_ID)
        self.assertEqual(h.venue_id, 42)
        self.assertEqual(h.date_utc, 20260522)
        self.assertEqual(h.producer_kind, "test-producer")

    def test_bad_magic_rejected(self):
        bad = self.path.with_suffix(".bad")
        with open(self.path, "rb") as src, open(bad, "wb") as dst:
            data = src.read()
            dst.write(b"XXXX" + data[4:])
        with self.assertRaises(ValueError) as ctx:
            read_header(bad)
        self.assertIn("not a canon file", str(ctx.exception))

    def test_iter_records_order_and_types(self):
        recs = list(iter_records(self.path))
        self.assertEqual(len(recs), 3)

        self.assertEqual(recs[0].ts_ns, 1_000_000_000)
        self.assertEqual(recs[0].event_type, EventType.BBO)

        self.assertEqual(recs[1].ts_ns, 1_500_000_000)
        self.assertEqual(recs[1].event_type, EventType.TRADE)

        self.assertEqual(recs[2].ts_ns, 2_000_000_000)
        self.assertEqual(recs[2].event_type, EventType.BBO)

    def test_bbo_payload_decodes(self):
        rec = next(r for r in iter_records(self.path) if r.event_type == EventType.BBO)
        body = rec.payload[SBE_MESSAGE_HEADER_BYTES : SBE_MESSAGE_HEADER_BYTES + _BBO_BLOCK_BYTES]
        _, iid, bid, bid_qty, ask, ask_qty, seq = struct.unpack(_BBO_FMT, body)
        self.assertEqual(iid, 12345)
        self.assertAlmostEqual(bid, 100.0)
        self.assertAlmostEqual(ask, 100.1)
        self.assertAlmostEqual(bid_qty, 5.0)
        self.assertAlmostEqual(ask_qty, 3.0)
        self.assertEqual(seq, 1)

    def test_trade_payload_decodes(self):
        rec = next(r for r in iter_records(self.path) if r.event_type == EventType.TRADE)
        body = rec.payload[SBE_MESSAGE_HEADER_BYTES : SBE_MESSAGE_HEADER_BYTES + _TRADE_BLOCK_BYTES]
        _, iid, price, qty, side, seq = struct.unpack(_TRADE_FMT, body)
        self.assertEqual(iid, 12345)
        self.assertAlmostEqual(price, 100.05)
        self.assertAlmostEqual(qty, 2.0)
        self.assertEqual(side, 0)  # BUY taker
        self.assertEqual(seq, 2)

    def test_truncated_file_raises(self):
        truncated = self.path.with_suffix(".trunc")
        with open(self.path, "rb") as src, open(truncated, "wb") as dst:
            data = src.read()
            # Cut off the last record halfway through its SBE payload
            dst.write(data[: FILE_HEADER_BYTES + RECORD_HEADER_BYTES + 4])
        with self.assertRaises(ValueError):
            list(iter_records(truncated))


if __name__ == "__main__":
    unittest.main()

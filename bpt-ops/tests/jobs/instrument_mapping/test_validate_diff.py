import json
from pathlib import Path

from bpt_ops.common.schema import ExchangeId
from bpt_ops.jobs.instrument_mapping import emit, reconcile, validate_diff
from bpt_ops.jobs.instrument_mapping.fetchers.base import RawInstrument


def _write(dir_: Path, raws: list[RawInstrument]) -> None:
    mapping = reconcile.build(raws, now_ms=1745000000000)
    emit.write_per_exchange(mapping, dir_)


def _full_universe() -> list[RawInstrument]:
    return [
        RawInstrument(ExchangeId.OKX, "BTC-USDT", "BTC", "USDT", "SPOT"),
        RawInstrument(ExchangeId.OKX, "ETH-USDT", "ETH", "USDT", "SPOT"),
        RawInstrument(ExchangeId.OKX, "SOL-USDT", "SOL", "USDT", "SPOT"),
        RawInstrument(ExchangeId.OKX, "XRP-USDT", "XRP", "USDT", "SPOT"),
        RawInstrument(ExchangeId.OKX, "DOGE-USDT", "DOGE", "USDT", "SPOT"),
    ]


def test_noop_diff_passes(tmp_path: Path):
    prev = tmp_path / "prev"
    new = tmp_path / "new"
    _write(prev, _full_universe())
    _write(new, _full_universe())

    findings = validate_diff.validate(prev, new, override=False)
    assert findings == []


def test_small_removal_passes(tmp_path: Path):
    # Remove 1 of 5 non-seed OKX rows (~20% change) — at the threshold
    prev = tmp_path / "prev"
    new = tmp_path / "new"
    _write(prev, _full_universe())
    _write(new, _full_universe()[:-1])  # drop DOGE

    findings = validate_diff.validate(prev, new, override=False)
    # Should pass: < 0.2 fractional threshold since seeds are retained
    assert not any("removed" in f for f in findings), findings


def test_mass_delete_fails_without_override(tmp_path: Path):
    prev = tmp_path / "prev"
    new = tmp_path / "new"
    _write(prev, _full_universe())
    _write(new, [])  # nuke everything — worst case

    findings = validate_diff.validate(prev, new, override=False)
    assert any("removed" in f for f in findings)


def test_mass_delete_passes_with_override(tmp_path: Path):
    prev = tmp_path / "prev"
    new = tmp_path / "new"
    _write(prev, _full_universe())
    _write(new, [])

    findings = validate_diff.validate(prev, new, override=True)
    # Removal should not surface; other checks still run
    assert not any("removed" in f for f in findings)


def test_canonical_id_renumbering_always_fails(tmp_path: Path):
    prev = tmp_path / "prev"
    new = tmp_path / "new"
    _write(prev, _full_universe())

    # Construct a corrupt new mapping where 2001 (BTC SPOT) now points at ETH
    mapping = reconcile.build(_full_universe(), now_ms=1745000000000)
    entry = mapping.reverse["2001"]
    entry.base = "ETH"  # identity-swap — canonical_id 2001 now means something else
    new.mkdir()
    payload = {
        "forward": {k: v for k, v in mapping.forward.items() if k.startswith("2_")},
        "reverse": {
            cid: ent.model_dump()
            for cid, ent in mapping.reverse.items()
            if any(k.startswith("2_") for k in mapping.forward if mapping.forward[k] == int(cid))
        },
        "exported_at": mapping.exported_at,
    }
    payload["instrument_count"] = len(payload["reverse"])
    (new / "instrument_mapping.okx.json").write_text(json.dumps(payload))

    # Override doesn't bypass id-stability — that check runs regardless
    findings = validate_diff.validate(prev, new, override=True)
    assert any("changed meaning" in f for f in findings)


def test_schema_violation_surfaces(tmp_path: Path):
    prev = tmp_path / "prev"
    new = tmp_path / "new"
    prev.mkdir()
    new.mkdir()

    (new / "instrument_mapping.okx.json").write_text('{"this": "is not the schema"}')

    findings = validate_diff.validate(prev, new, override=True)
    assert any("schema violation" in f for f in findings)

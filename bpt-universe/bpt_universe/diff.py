from __future__ import annotations

from pathlib import Path

import polars as pl
import tomlkit


# Strategy config uses "BTC/USDT:SPOT" / "BTC-USDT-SWAP:PERP" — symbol + type
# joined by colon. Reproduce that format so the diff round-trips cleanly into
# bpt-strategy/config/strategies/avellaneda_stoikov.toml.
def _config_symbol(row: dict) -> str:
    return f"{row['symbol']}:{row['inst_type']}"


def proposed_instruments(candidates: pl.DataFrame, top_n: int | None = None) -> list[str]:
    rows = candidates.to_dicts()
    if top_n is not None:
        rows = rows[:top_n]
    return [_config_symbol(r) for r in rows]


def render_diff(
    current_config: Path,
    candidates: pl.DataFrame,
    top_n: int | None = None,
) -> tuple[str, list[str], list[str]]:
    """Produce a unified-diff-shaped string + the add/remove lists.

    The diff is text the operator pastes into a PR description; the add/remove
    lists drive the report. We deliberately do NOT rewrite the TOML in place —
    the human reviewer commits the change, not this tool.
    """
    doc = tomlkit.parse(Path(current_config).read_text())
    current = list(doc.get("instruments") or [])
    proposed = proposed_instruments(candidates, top_n=top_n)

    cur_set, prop_set = set(current), set(proposed)
    to_add = sorted(prop_set - cur_set)
    to_remove = sorted(cur_set - prop_set)

    lines = [f"--- {current_config}", f"+++ {current_config} (proposed)"]
    lines.append("@@ instruments @@")
    for s in current:
        lines.append(f" {s}" if s in prop_set else f"-{s}")
    for s in proposed:
        if s not in cur_set:
            lines.append(f"+{s}")
    return "\n".join(lines) + "\n", to_add, to_remove

from __future__ import annotations

from dataclasses import dataclass, field

import polars as pl


@dataclass(frozen=True)
class ScoringConfig:
    # Hard filters — instrument is dropped before scoring if any fail.
    venues: tuple[str, ...] = ("OKX", "HYPERLIQUID")
    inst_types: tuple[str, ...] = ("PERP", "SPOT")
    quote_ccys: tuple[str, ...] = ("USDT", "USDC", "USD")
    min_samples: int = 100  # MD samples required to trust spread features
    min_fills: int = 20  # fills required to trust markout features

    # Soft thresholds.
    min_score: float = 0.0  # final score floor for inclusion in shortlist
    min_realized_capture_bps: float | None = None  # if set, fills + capture > X

    # Score weights. Each component contributes only if its inputs are present;
    # absent columns drop out of the sum cleanly so refdata-only and tape-fed
    # runs both produce coherent rankings.
    #
    # realized_capture_bps already encodes markout − fee, so we don't double-
    # count by listing them separately. Sign convention: positive = profitable
    # for the maker (mid moved in the held direction) net of fees.
    weights: dict[str, float] = field(
        default_factory=lambda: {
            "spread_bps_p50": 0.5,  # wider median spread → more potential edge
            "realized_capture_bps": 1.0,  # measured profit per fill, post-fee
            "tick_floor_bps": -0.2,  # tight ticks bound how much spread we can quote
        }
    )


def _join_features(
    refdata: pl.DataFrame,
    md: pl.DataFrame | None,
    fills: pl.DataFrame | None,
) -> pl.DataFrame:
    out = refdata
    if md is not None:
        out = out.join(md, on="instrument_id", how="left")
    if fills is not None:
        out = out.join(fills, on="instrument_id", how="left")
    return out


def _add_derived(df: pl.DataFrame) -> pl.DataFrame:
    # tick_floor_bps requires a mid; computed lazily so refdata-only runs work too.
    if "mid_mean" not in df.columns:
        return df.with_columns(tick_floor_bps=pl.lit(None, dtype=pl.Float64))
    return df.with_columns(
        tick_floor_bps=pl.when(pl.col("mid_mean") > 0)
        .then(pl.col("tick_size") / pl.col("mid_mean") * 10_000)
        .otherwise(None),
    )


def _apply_filters(df: pl.DataFrame, cfg: ScoringConfig) -> pl.DataFrame:
    out = df.filter(
        pl.col("exchange").is_in(list(cfg.venues))
        & pl.col("inst_type").is_in(list(cfg.inst_types))
        & pl.col("quote_ccy").is_in(list(cfg.quote_ccys))
    )
    if "samples" in out.columns:
        out = out.filter(pl.col("samples").fill_null(0) >= cfg.min_samples)
    if "fills" in out.columns:
        out = out.filter(pl.col("fills").fill_null(0) >= cfg.min_fills)
    if cfg.min_realized_capture_bps is not None and "realized_capture_bps" in out.columns:
        out = out.filter(pl.col("realized_capture_bps") >= cfg.min_realized_capture_bps)
    return out


def _score_expr(cfg: ScoringConfig, available_cols: set[str]) -> pl.Expr:
    """Build the weighted-sum score over whichever feature columns are present.

    Missing inputs (e.g. no MD = no spread features) drop out cleanly: their
    weight contributes 0 instead of NaN. This is what lets refdata-only runs
    still produce a coherent ranking.
    """
    terms = [pl.lit(0.0)]
    for col, w in cfg.weights.items():
        if col not in available_cols:
            continue
        terms.append(pl.col(col).fill_null(0.0) * w)
    expr = terms[0]
    for t in terms[1:]:
        expr = expr + t
    return expr.alias("score")


def score(
    refdata: pl.DataFrame,
    md: pl.DataFrame | None = None,
    fills: pl.DataFrame | None = None,
    cfg: ScoringConfig | None = None,
) -> pl.DataFrame:
    """Produce the ranked candidate frame.

    Output is sorted by score descending and includes every feature that fed
    the score so a human reviewing the diff can see *why* an instrument moved.
    """
    cfg = cfg or ScoringConfig()
    joined = _join_features(refdata, md, fills)
    derived = _add_derived(joined)
    filtered = _apply_filters(derived, cfg)
    cols = set(filtered.columns)
    out = filtered.with_columns(_score_expr(cfg, cols))
    out = out.filter(pl.col("score") >= cfg.min_score)
    return out.sort("score", descending=True)

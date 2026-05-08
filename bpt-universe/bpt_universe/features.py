from __future__ import annotations

import polars as pl


def md_features(md: pl.LazyFrame) -> pl.DataFrame:
    """Aggregate per-instrument BBO-level features over the sample window.

    Output columns:
      instrument_id, samples, mid_mean, spread_bps_mean, spread_bps_p50,
      spread_bps_p95, depth_top_mean.

    spread_bps = (ask - bid) / mid * 10000. Clipped at 0 to discard the rare
    crossed-quote samples a tape can carry during a venue outage.
    """
    return (
        md.with_columns(
            mid=(pl.col("best_bid") + pl.col("best_ask")) / 2.0,
            spread=pl.col("best_ask") - pl.col("best_bid"),
        )
        .with_columns(
            spread_bps=pl.when(pl.col("spread") > 0)
            .then(pl.col("spread") / pl.col("mid") * 10_000)
            .otherwise(None),
            depth_top=pl.col("bid_size") + pl.col("ask_size"),
        )
        .group_by("instrument_id")
        .agg(
            samples=pl.len(),
            mid_mean=pl.col("mid").mean(),
            spread_bps_mean=pl.col("spread_bps").mean(),
            spread_bps_p50=pl.col("spread_bps").median(),
            spread_bps_p95=pl.col("spread_bps").quantile(0.95),
            depth_top_mean=pl.col("depth_top").mean(),
        )
        .collect()
    )


def fill_features(fills: pl.LazyFrame, horizon_s: int = 60) -> pl.DataFrame:
    """Per-instrument markout summary at a single horizon.

    Markout convention: positive number = profitable from the maker's perspective
    (bought below mid, mid drifted down means we got adversely selected; bought
    below mid, mid drifted up = we captured edge).

    Output: instrument_id, fills, fee_bps_mean, markout_bps_mean, markout_bps_std,
    realized_capture_bps. realized_capture_bps = markout − fee, the metric the
    AS book actually optimizes against.
    """
    col = f"mid_{horizon_s}s_after"
    sign = pl.when(pl.col("side") == "BUY").then(1.0).otherwise(-1.0)
    return (
        fills.with_columns(
            markout_bps=sign * (pl.col(col) - pl.col("price")) / pl.col("mid_at_fill") * 10_000,
            fee_bps=pl.col("fee_paid") / (pl.col("price") * pl.col("qty")) * 10_000,
        )
        .group_by("instrument_id")
        .agg(
            fills=pl.len(),
            fee_bps_mean=pl.col("fee_bps").mean(),
            markout_bps_mean=pl.col("markout_bps").mean(),
            markout_bps_std=pl.col("markout_bps").std(),
        )
        .with_columns(
            realized_capture_bps=pl.col("markout_bps_mean") - pl.col("fee_bps_mean"),
        )
        .collect()
    )


def metadata_features(refdata: pl.DataFrame) -> pl.DataFrame:
    """Refdata-only features: derivable without any tape data.

    tick_floor_bps is the structural lower bound on quotable spread — a market
    where one tick is 0.01% of price can never be quoted tighter than 1 bp.
    Useful even before MD samples land because it filters out instruments that
    are too tightly priced to MM profitably regardless of behavior.

    mid_proxy is intentionally absent here: refdata has no price. tick_floor_bps
    is computed in scoring.py once an MD-sourced mid is joined in.
    """
    return refdata.select(
        "instrument_id",
        "exchange",
        "symbol",
        "inst_type",
        "tick_size",
        "lot_size",
        "min_size",
        "quote_ccy",
    )

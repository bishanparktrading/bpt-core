"""Statistical significance for backtest results.

Wraps Bailey & Lopez de Prado's PSR/DSR framework around the per-fill
returns we get out of the deterministic backtester. Use this before
making any "this strategy works" claim from a sweep.
"""

from .significance import (
    deflated_sharpe_ratio,
    expected_max_sharpe,
    probabilistic_sharpe_ratio,
    sharpe_ratio,
)

__all__ = [
    "deflated_sharpe_ratio",
    "expected_max_sharpe",
    "probabilistic_sharpe_ratio",
    "sharpe_ratio",
]

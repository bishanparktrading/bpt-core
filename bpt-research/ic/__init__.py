"""Signal-evaluation primitives for strategy discovery.

The IC (Information Coefficient) panel + decorations — quantile bins for
monotonicity, lead-lag scan for lookahead detection. Calls into the same
C++ feature impls AS uses (via bpt_features pybind11) so research and
production can't diverge silently.
"""

from .features import ofi, microprice_dev, drift, sigma2, trade_flow_ewma  # noqa: F401
from .panel import ic_panel, aggregate  # noqa: F401
from .quantile import quantile_analysis, monotonicity_score  # noqa: F401
from .lead_lag import lead_lag_ic  # noqa: F401
from .multivariate import (  # noqa: F401
    partial_slopes,
    feature_correlation,
    fit_composite_weights,
    composite_signal,
)

__all__ = [
    "ofi", "microprice_dev", "drift", "sigma2", "trade_flow_ewma",
    "ic_panel", "aggregate",
    "quantile_analysis", "monotonicity_score",
    "lead_lag_ic",
    "partial_slopes", "feature_correlation",
    "fit_composite_weights", "composite_signal",
]

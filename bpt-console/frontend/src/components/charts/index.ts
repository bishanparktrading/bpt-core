// Main-chart registry. Parallel to components/panels/index.ts —
// every strategy that wants its own primary chart (instead of the
// default single-instrument candlestick view) registers it here keyed
// by the StrategyKind discriminator. App.tsx looks up the right chart
// for the active strategy and renders it in the main-row slot.

import type { FC } from 'react'
import type { StrategyKind } from '../../types/messages'
import { PriceChart } from '../PriceChart'
import { DualLegChart } from './DualLegChart'
import { OptionsMakerChart } from './OptionsMakerChart'

// AS is currently the only strategy whose chart needs AS-specific
// overlays (resting bid/ask + reservation lines). PriceChart already
// narrows to AS internally — registering it here also serves as the
// default when a strategy is registered without a bespoke chart.
//
// OptionsMaker doesn't have a single-instrument tick stream — it
// quotes a chain of strikes across multiple underlyings. Its chart
// plots per-UL book_delta over time from the strategyState frames
// instead of tick events.
export const STRATEGY_CHARTS: Partial<Record<StrategyKind, FC>> = {
  AS: PriceChart,
  FundingArb: DualLegChart,
  OptionsMaker: OptionsMakerChart,
}

// Default chart when there's no active strategy (or when the kind has
// no registered chart). PriceChart works generically as a single-
// instrument candlestick view fed by tick events.
export const DefaultChart = PriceChart

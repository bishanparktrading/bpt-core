import { useEffect, useRef } from 'react'
import {
  createChart,
  LineSeries,
  ColorType,
  CrosshairMode,
  LineStyle,
  type IChartApi,
  type IPriceLine,
  type ISeriesApi,
  type UTCTimestamp,
} from 'lightweight-charts'
import { useStore } from '../../store'

// Mainchart for kind='OptionsMaker'. Plots book_delta over time per
// (exchange, underlying). Book_delta = portfolio_delta + perp_position
// is the load-bearing risk number for an options MM with embedded perp
// hedger — goal is to stay near 0, inside the operator's hedge
// threshold band.
//
// Why not PriceChart: AS's chart is fed by the bridge's single-
// instrument tick stream (filtered server-side by settings.instrument_id).
// OptionsMaker quotes 6+ strikes × 2 underlyings concurrently — there
// is no single instrument that "represents" the strategy. book_delta
// is computed strategy-side and arrives in strategyState frames, so we
// drive the chart from there instead of trying to thread a multi-
// instrument tick stream through the bridge.

const CHART_THEME = {
  bg: '#0d1117',
  text: '#768390',
  grid: '#141b26',
  border: '#1c2333',
  crosshair: '#243044',
  // Display-only threshold matching OptionsMakerPanel's bookDeltaClass
  // amber band start (0.01) and red band start (0.05). The strategy
  // enforces its own gate; these are visual anchors for the operator.
  thresholdAmber: '#9a6914',
  thresholdRed: '#5a1e22',
  // Per-UL line colours. Picked to be distinguishable from each other
  // and from the AS / FundingArb palettes — BTC orange, ETH blue, then
  // a cycle for any future ULs.
  ulCycle: ['#e89436', '#5c9ce6', '#3fb950', '#d4a72c', '#a371f7'] as const,
}

// Display-only band magnitude (base-coin units). Matches the amber
// threshold in OptionsMakerPanel.bookDeltaClass — anything inside this
// band is "neutral enough" to the human eye.
const AMBER_BAND = 0.05

export function OptionsMakerChart() {
  const bookDeltaHistory = useStore((s) => s.bookDeltaHistory)
  const strategyState = useStore((s) => s.strategyState)
  const om = strategyState?.kind === 'OptionsMaker' ? strategyState : null

  const hostRef = useRef<HTMLDivElement>(null)
  const chartRef = useRef<IChartApi | null>(null)
  // One LineSeries per "EXCHANGE|UNDERLYING" key. Built lazily as keys
  // first appear in bookDeltaHistory; removed when a key disappears
  // (strategy was reconfigured to drop an underlying).
  const seriesByKeyRef = useRef<Map<string, ISeriesApi<'Line'>>>(new Map())
  const upperBandRef = useRef<IPriceLine | null>(null)
  const lowerBandRef = useRef<IPriceLine | null>(null)
  const zeroLineRef = useRef<IPriceLine | null>(null)
  const didFitRef = useRef(false)

  // Create the chart once on mount, dispose on unmount.
  useEffect(() => {
    if (!hostRef.current) return
    const chart = createChart(hostRef.current, {
      layout: {
        background: { type: ColorType.Solid, color: CHART_THEME.bg },
        textColor: CHART_THEME.text,
        fontFamily: '"SF Mono", "Fira Code", Menlo, monospace',
        fontSize: 11,
      },
      grid: {
        vertLines: { color: CHART_THEME.grid },
        horzLines: { color: CHART_THEME.grid },
      },
      rightPriceScale: {
        borderColor: CHART_THEME.border,
      },
      timeScale: {
        borderColor: CHART_THEME.border,
        timeVisible: true,
        secondsVisible: true,
      },
      crosshair: {
        mode: CrosshairMode.Normal,
        vertLine: { color: CHART_THEME.crosshair, width: 1, style: 3 },
        horzLine: { color: CHART_THEME.crosshair, width: 1, style: 3 },
      },
    })
    chartRef.current = chart

    const ro = new ResizeObserver((entries) => {
      for (const e of entries) chart.resize(e.contentRect.width, e.contentRect.height)
    })
    ro.observe(hostRef.current)

    return () => {
      ro.disconnect()
      chart.remove()
      chartRef.current = null
      seriesByKeyRef.current.clear()
      upperBandRef.current = null
      lowerBandRef.current = null
      zeroLineRef.current = null
      didFitRef.current = false
    }
  }, [])

  // Sync line series with bookDeltaHistory keys. We use setData on each
  // update for the freshly-mounted case (chart can replay full history
  // post-HMR) — for steady-state appends, lightweight-charts is fast
  // enough at setData on 1200 points that the lighter .update() isn't
  // a meaningful win. Keeping setData also handles the case where a
  // new UL appears mid-session (we don't have to track "first sample
  // vs subsequent" per series).
  useEffect(() => {
    const chart = chartRef.current
    if (!chart) return

    const keys = Object.keys(bookDeltaHistory)

    // Add series for new keys.
    keys.forEach((key, idx) => {
      if (seriesByKeyRef.current.has(key)) return
      const colour = CHART_THEME.ulCycle[idx % CHART_THEME.ulCycle.length]
      const series = chart.addSeries(LineSeries, {
        color: colour,
        lineWidth: 2,
        title: key.split('|')[1] ?? key,  // strip "EXCHANGE|" prefix for the chart legend
        priceFormat: { type: 'price', precision: 4, minMove: 0.0001 },
      })
      seriesByKeyRef.current.set(key, series)
    })

    // Remove series whose key vanished from the store.
    for (const [key, series] of seriesByKeyRef.current) {
      if (!keys.includes(key)) {
        chart.removeSeries(series)
        seriesByKeyRef.current.delete(key)
      }
    }

    // Push data for each live series.
    for (const key of keys) {
      const series = seriesByKeyRef.current.get(key)
      if (!series) continue
      const samples = bookDeltaHistory[key]
      series.setData(
        samples.map((s) => ({
          time: s.ts as UTCTimestamp,
          value: s.bookDelta,
        })),
      )
    }

    // Auto-fit once we have a meaningful chunk of data.
    const totalPoints = keys.reduce((acc, k) => acc + (bookDeltaHistory[k]?.length ?? 0), 0)
    if (!didFitRef.current && totalPoints >= 10) {
      chart.timeScale().fitContent()
      didFitRef.current = true
    }
  }, [bookDeltaHistory])

  // Threshold bands + zero line. Anchored to any one of the series — we
  // pick the first because price-lines are attached per series in
  // lightweight-charts but render across the whole chart area.
  useEffect(() => {
    const firstSeries = seriesByKeyRef.current.values().next().value
    if (!firstSeries) return

    const ensureLine = (
      ref: React.MutableRefObject<IPriceLine | null>,
      price: number,
      color: string,
      title: string,
      style: LineStyle = LineStyle.Dashed,
    ) => {
      const opts = {
        price,
        color,
        lineWidth: 1 as const,
        lineStyle: style,
        axisLabelVisible: true,
        title,
      }
      if (ref.current) {
        ref.current.applyOptions(opts)
      } else {
        ref.current = firstSeries.createPriceLine(opts)
      }
    }
    ensureLine(zeroLineRef, 0, CHART_THEME.text, 'neutral', LineStyle.Dotted)
    ensureLine(upperBandRef, AMBER_BAND, CHART_THEME.thresholdAmber, `+${AMBER_BAND}`)
    ensureLine(lowerBandRef, -AMBER_BAND, CHART_THEME.thresholdAmber, `-${AMBER_BAND}`)
  }, [bookDeltaHistory])

  const ulCount = om?.underlyings.length ?? 0

  return (
    <div className="panel">
      <div className="panel-header">
        <span className="panel-title">Book Δ · {ulCount > 0 ? `${ulCount} UL` : '—'}</span>
        <span className="panel-badge">
          {om?.risk_halted ? 'RISK HALTED' : 'OptionsMaker'}
        </span>
      </div>
      <div className="chart-host" ref={hostRef} />
    </div>
  )
}

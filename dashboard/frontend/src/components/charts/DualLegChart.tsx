import { useEffect, useRef } from 'react'
import {
  createChart,
  LineSeries,
  ColorType,
  CrosshairMode,
  type IChartApi,
  type ISeriesApi,
  type UTCTimestamp,
} from 'lightweight-charts'
import { useStore } from '../../store'

// Mainchart for FundingArb-style strategies. Plots both legs (spot in
// blue, perp in orange) on the same axis so the visual gap matches the
// basis-bps number in the strategy-state panel. Fed by strategyState
// frames (2 Hz), not by md_data ticks — the bridge labels every BBO
// with one symbol, so going through the tick stream would aggregate
// both legs into a single line. Driving from the state JSON keeps the
// two legs distinct.

const CHART_THEME = {
  bg: '#0d1117',
  text: '#768390',
  grid: '#141b26',
  border: '#1c2333',
  crosshair: '#243044',
  spot: '#5c9ce6',
  perp: '#e89436',
}

// 30 minutes at 2 Hz. Bigger than the in-panel version (5 min) since
// this chart owns the full main-row slot.
const MAX_POINTS = 3600

export function DualLegChart() {
  const strategyState = useStore((s) => s.strategyState)
  // Only render leg history when the active strategy is funding-arb
  // shaped. Anything else feeds zero into the series and would skew
  // the price scale.
  const fa = strategyState?.kind === 'FundingArb' ? strategyState : null

  const hostRef = useRef<HTMLDivElement>(null)
  const chartRef = useRef<IChartApi | null>(null)
  const spotRef = useRef<ISeriesApi<'Line'> | null>(null)
  const perpRef = useRef<ISeriesApi<'Line'> | null>(null)
  const lastTsRef = useRef<number>(0)
  const pointCountRef = useRef<number>(0)

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
      rightPriceScale: { borderColor: CHART_THEME.border },
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
    spotRef.current = chart.addSeries(LineSeries, {
      color: CHART_THEME.spot,
      lineWidth: 2,
      title: 'spot',
    })
    perpRef.current = chart.addSeries(LineSeries, {
      color: CHART_THEME.perp,
      lineWidth: 2,
      title: 'perp',
    })
    chartRef.current = chart

    const ro = new ResizeObserver(() => {
      if (hostRef.current && chartRef.current) {
        chartRef.current.applyOptions({
          width: hostRef.current.clientWidth,
          height: hostRef.current.clientHeight,
        })
      }
    })
    ro.observe(hostRef.current)

    return () => {
      ro.disconnect()
      chart.remove()
      chartRef.current = null
      spotRef.current = null
      perpRef.current = null
    }
  }, [])

  useEffect(() => {
    if (!fa) return
    const spot = spotRef.current
    const perp = perpRef.current
    if (!spot || !perp) return
    let ts = Math.floor(Date.now() / 1000)
    if (ts <= lastTsRef.current) ts = lastTsRef.current + 1
    lastTsRef.current = ts
    const t = ts as UTCTimestamp
    if (fa.spotPx > 0) spot.update({ time: t, value: fa.spotPx })
    if (fa.perpPx > 0) perp.update({ time: t, value: fa.perpPx })
    pointCountRef.current += 1
    // Auto-fit only on the first MAX_POINTS frames so the operator can
    // pan/zoom freely afterwards without the view snapping back.
    if (pointCountRef.current < MAX_POINTS && chartRef.current) {
      chartRef.current.timeScale().fitContent()
    }
  }, [fa, fa?.spotPx, fa?.perpPx])

  return (
    <div className="panel">
      <div className="panel-header">
        <span className="panel-title">
          {fa ? `${fa.symbol} · spot vs perp` : 'spot vs perp'}
        </span>
        <span className="panel-badge">
          {fa
            ? `basis ${fa.basisBps >= 0 ? '+' : ''}${fa.basisBps.toFixed(1)} bps`
            : '—'}
        </span>
      </div>
      <div ref={hostRef} style={{ width: '100%', height: '100%', flex: 1 }} />
    </div>
  )
}

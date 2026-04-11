import { useEffect, useRef } from 'react'
import {
  createChart,
  CandlestickSeries,
  createSeriesMarkers,
  ColorType,
  CrosshairMode,
  type IChartApi,
  type ISeriesApi,
  type ISeriesMarkersPluginApi,
  type SeriesMarker,
  type Time,
  type UTCTimestamp,
} from 'lightweight-charts'
import type { Candle } from '../mock/candles'
import { useStore } from '../store'

interface PriceChartProps {
  candles: Candle[]
}

const CHART_THEME = {
  bg:        '#0d1117',
  text:      '#768390',
  grid:      '#141b26',
  border:    '#1c2333',
  up:        '#3fb950',
  down:      '#f85149',
  crosshair: '#243044',
}

export function PriceChart({ candles }: PriceChartProps) {
  const fills = useStore((s) => s.fills)
  const hostRef = useRef<HTMLDivElement>(null)
  const chartRef = useRef<IChartApi | null>(null)
  const seriesRef = useRef<ISeriesApi<'Candlestick'> | null>(null)
  const markersRef = useRef<ISeriesMarkersPluginApi<Time> | null>(null)
  const didFitRef = useRef(false)

  // Create chart + set static candles once.
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
        secondsVisible: false,
      },
      crosshair: {
        mode: CrosshairMode.Normal,
        vertLine: { color: CHART_THEME.crosshair, width: 1, style: 3 },
        horzLine: { color: CHART_THEME.crosshair, width: 1, style: 3 },
      },
    })

    const series = chart.addSeries(CandlestickSeries, {
      upColor: CHART_THEME.up,
      downColor: CHART_THEME.down,
      borderUpColor: CHART_THEME.up,
      borderDownColor: CHART_THEME.down,
      wickUpColor: CHART_THEME.up,
      wickDownColor: CHART_THEME.down,
    })

    chartRef.current = chart
    seriesRef.current = series
    markersRef.current = createSeriesMarkers(series, [])

    const ro = new ResizeObserver((entries) => {
      for (const e of entries) chart.resize(e.contentRect.width, e.contentRect.height)
    })
    ro.observe(hostRef.current)

    return () => {
      ro.disconnect()
      chart.remove()
      chartRef.current = null
      seriesRef.current = null
      markersRef.current = null
      didFitRef.current = false
    }
  }, [])

  // Set candles once (the full backtest range is known up front).
  useEffect(() => {
    const series = seriesRef.current
    if (!series) return
    series.setData(
      candles.map((c) => ({
        time: c.time as UTCTimestamp,
        open: c.open,
        high: c.high,
        low: c.low,
        close: c.close,
      })),
    )
    if (!didFitRef.current) {
      chartRef.current?.timeScale().fitContent()
      didFitRef.current = true
    }
  }, [candles])

  // Stream markers as fills arrive — no chart reload, just updating the marker layer.
  useEffect(() => {
    const markers = markersRef.current
    if (!markers) return
    const data: SeriesMarker<Time>[] = fills.map((f) => ({
      time: Math.floor(f.ts / 1_000_000_000) as UTCTimestamp,
      position: f.side === 'BUY' ? 'belowBar' : 'aboveBar',
      color: f.side === 'BUY' ? CHART_THEME.up : CHART_THEME.down,
      shape: f.side === 'BUY' ? 'arrowUp' : 'arrowDown',
      text: `${f.side[0]} ${f.qty}`,
    }))
    markers.setMarkers(data)
  }, [fills])

  return <div className="chart-host" ref={hostRef} />
}

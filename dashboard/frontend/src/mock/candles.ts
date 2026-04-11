// Synthetic 1-minute candle generator for the mock dashboard.
//
// Given the fill timeline from trades.csv, produce candles that pass through
// the fill prices and fill the gaps with a correlated random walk.  Purely
// cosmetic — the real dashboard reads OHLC from the Huginn MD stream via the
// bridge.

import type { Fill } from '../components/Blotter'

export interface Candle {
  time: number   // unix seconds (lightweight-charts convention)
  open: number
  high: number
  low: number
  close: number
}

const BAR_MS = 60_000              // 1-minute bars
const NS_PER_MS = 1_000_000
const PAD_BARS = 60                // 1h of padding before/after the fill window

// Linearly interpolate between the closest fills so the random walk has an
// anchor it is pulled toward.
function anchorPrice(tsMs: number, fills: Fill[]): number {
  let prev = fills[0]
  let next = fills[fills.length - 1]
  for (let i = 0; i < fills.length; i++) {
    const fMs = fills[i].ts / NS_PER_MS
    if (fMs <= tsMs) prev = fills[i]
    if (fMs >= tsMs) { next = fills[i]; break }
  }
  const prevMs = prev.ts / NS_PER_MS
  const nextMs = next.ts / NS_PER_MS
  if (prevMs === nextMs) return prev.price
  const t = (tsMs - prevMs) / (nextMs - prevMs)
  return prev.price + t * (next.price - prev.price)
}

// Cheap deterministic PRNG so the mock chart looks identical on every reload.
function mulberry32(seed: number) {
  return () => {
    let t = (seed += 0x6D2B79F5)
    t = Math.imul(t ^ (t >>> 15), t | 1)
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61)
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}

export function generateCandles(fills: Fill[]): Candle[] {
  if (fills.length === 0) return []

  const rnd = mulberry32(0xBEEF)
  const startMs = Math.floor(fills[0].ts / NS_PER_MS / BAR_MS) * BAR_MS - PAD_BARS * BAR_MS
  const endMs   = Math.ceil(fills[fills.length - 1].ts / NS_PER_MS / BAR_MS) * BAR_MS + PAD_BARS * BAR_MS

  const candles: Candle[] = []
  let lastClose = fills[0].price

  for (let tsMs = startMs; tsMs < endMs; tsMs += BAR_MS) {
    const anchor = anchorPrice(tsMs + BAR_MS / 2, fills)

    // Random walk pulled toward the anchor (OU-style)
    const pull = 0.35
    const noise = (rnd() - 0.5) * anchor * 0.0015   // ±0.075% per-bar jitter
    const drift = (anchor - lastClose) * pull
    const open = lastClose
    const close = open + drift + noise

    const range = anchor * 0.0012 * (0.6 + rnd() * 0.8)
    const high = Math.max(open, close) + rnd() * range
    const low  = Math.min(open, close) - rnd() * range

    candles.push({
      time: Math.floor(tsMs / 1000),
      open, high, low, close,
    })
    lastClose = close
  }

  return candles
}

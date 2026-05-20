import { useStore, type ToxicitySample } from '../store'

// ───────────────────── Markout sparkline ─────────────────────────────
// Inline SVG, no chart library — a sparkline this small doesn't need
// axes, ticks, or interactivity. Two polylines (bid / ask) over a zero
// reference line. NaN samples (warmup) create a gap rather than a fake
// zero line through the chart.

interface SparklineProps {
  history: ToxicitySample[]
  // Side determines colour + which field is read.
  side: 'bid' | 'ask'
}

const SPARKLINE_W = 220
const SPARKLINE_H = 36
// Hard ±5 bps y-axis bounds. Markouts beyond ±5 bps are extreme — clamping
// keeps the typical-day shape readable rather than the y-axis being
// stretched by occasional outliers. The actual numeric value is in the
// table above; this is for trend at a glance.
const Y_BOUND_BPS = 5

function MarkoutSparkline({ history, side }: SparklineProps) {
  if (history.length < 2) {
    return (
      <div
        style={{
          width: SPARKLINE_W,
          height: SPARKLINE_H,
          fontSize: 9,
          color: 'var(--text-muted)',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
        }}
      >
        warming up…
      </div>
    )
  }

  const getValue = (s: ToxicitySample) => (side === 'bid' ? s.bidMarkout5s : s.askMarkout5s)
  const colour = side === 'bid' ? 'var(--green)' : 'var(--red)'

  // Map each sample to (x, y) in SVG coordinates. Gap detection: split
  // the polyline at any NaN so we don't draw across warmup gaps.
  const segments: string[] = []
  let current: string[] = []
  history.forEach((s, i) => {
    const v = getValue(s)
    if (Number.isNaN(v) || v == null) {
      if (current.length > 0) {
        segments.push(current.join(' '))
        current = []
      }
      return
    }
    const x = (i / (history.length - 1)) * SPARKLINE_W
    const clamped = Math.max(-Y_BOUND_BPS, Math.min(Y_BOUND_BPS, v))
    // y=0 at top, y=H at bottom; positive bps = top half, negative = bottom.
    const y = SPARKLINE_H / 2 - (clamped / Y_BOUND_BPS) * (SPARKLINE_H / 2)
    current.push(`${x.toFixed(1)},${y.toFixed(1)}`)
  })
  if (current.length > 0) segments.push(current.join(' '))

  return (
    <svg
      width={SPARKLINE_W}
      height={SPARKLINE_H}
      viewBox={`0 0 ${SPARKLINE_W} ${SPARKLINE_H}`}
      style={{ display: 'block' }}
    >
      {/* Zero reference line — favourable above, adverse below. */}
      <line
        x1={0}
        x2={SPARKLINE_W}
        y1={SPARKLINE_H / 2}
        y2={SPARKLINE_H / 2}
        stroke="var(--border)"
        strokeDasharray="2,2"
      />
      {segments.map((pts, idx) => (
        <polyline key={idx} points={pts} fill="none" stroke={colour} strokeWidth={1.5} />
      ))}
    </svg>
  )
}

type Num = number | null | undefined
const nullish = (v: Num): v is null | undefined => v == null || isNaN(v as number)

const fmt1 = (v: Num) => (nullish(v) ? '—' : (v as number).toFixed(1))
const fmtPct = (v: Num) => (nullish(v) ? '—' : `${((v as number) * 100).toFixed(0)}%`)
const fmtScore = (v: Num) => (nullish(v) ? '—' : (v as number).toFixed(2))
const fmtMs = (v: Num) => (nullish(v) ? '—' : `${(v as number).toFixed(0)}ms`)

function scoreClass(v: Num): string {
  if (nullish(v)) return 'stat-value--muted'
  if ((v as number) > 0) return 'stat-value--green'
  if ((v as number) > -2) return 'stat-value--muted'
  return 'stat-value--red'
}

function rateClass(v: Num): string {
  if (nullish(v)) return 'stat-value--muted'
  if ((v as number) < 0.4) return 'stat-value--green'
  if ((v as number) < 0.6) return 'stat-value--muted'
  return 'stat-value--red'
}

function fillRateClass(v: Num): string {
  if (nullish(v)) return 'stat-value--muted'
  if ((v as number) > 0.8) return 'stat-value--red'
  if ((v as number) > 0.5) return 'stat-value--muted'
  return 'stat-value--green'
}

function ttfClass(v: Num): string {
  if (nullish(v)) return 'stat-value--muted'
  if ((v as number) < 500) return 'stat-value--red'
  if ((v as number) < 2000) return 'stat-value--muted'
  return 'stat-value--green'
}

// Sample threshold below which markout / adverse / score are statistically
// meaningless — they'd render as 0.0 which is indistinguishable from "flat
// market" but actually means "we only have N<5 fills, trust nothing." The
// number matches scorer_min_samples = 5 in bpt-analytics/config/bpt-analytics.*.toml.
// Plumbing the exact threshold through the message would be more correct
// but the config rarely changes; hardcoding keeps the panel self-contained.
const MIN_SAMPLES = 5

export function ToxicityPanel() {
  const tox = useStore((s) => s.toxicity)
  const history = useStore((s) => s.toxicityHistory)

  if (!tox) {
    return (
      <div className="panel" style={{ gridArea: 'toxicity' }}>
        <div className="panel-header">
          <span className="panel-title">Flow Toxicity</span>
          <span className="panel-badge">TOX</span>
        </div>
        <div style={{ padding: '12px 16px', color: 'var(--text-muted)', fontSize: 13 }}>
          Waiting for analytics data.
        </div>
      </div>
    )
  }

  // Per-side warmup gate: when samples are sparse, mask the markout /
  // adverse / score cells with '—' rather than letting a single-sample
  // zero read as "flat market." Fill rate + TTF + sample count stay
  // visible because they converge meaningfully even at n=1.
  const bidWarmup = tox.bidSamples < MIN_SAMPLES
  const askWarmup = tox.askSamples < MIN_SAMPLES
  const mask = (v: Num, warmup: boolean): Num => (warmup ? null : v)

  const bothWarm = !bidWarmup && !askWarmup
  const badgeText = bothWarm
    ? `TOX · ${tox.bidSamples + tox.askSamples} fills`
    : `WARMUP · bid ${tox.bidSamples}/${MIN_SAMPLES} ask ${tox.askSamples}/${MIN_SAMPLES}`
  const badgeClass = bothWarm ? 'panel-badge' : 'panel-badge panel-badge--warn'

  return (
    <div className="panel" style={{ gridArea: 'toxicity' }}>
      <div className="panel-header">
        <span className="panel-title">Flow Toxicity</span>
        <span className={badgeClass}>{badgeText}</span>
      </div>
      <table className="blotter-table" style={{ fontSize: 11 }}>
        <thead>
          <tr>
            <th></th>
            <th className="num">Markout 5s</th>
            <th className="num">Adverse %</th>
            <th className="num">Score</th>
            <th className="num">Fill Rate</th>
            <th className="num">TTF</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td style={{ fontWeight: 600 }}>BID</td>
            <td className={`num ${scoreClass(mask(tox.bidMarkout5s, bidWarmup))}`}>
              {fmt1(mask(tox.bidMarkout5s, bidWarmup))} bps
            </td>
            <td className={`num ${rateClass(mask(tox.bidAdverseRate, bidWarmup))}`}>
              {fmtPct(mask(tox.bidAdverseRate, bidWarmup))}
            </td>
            <td className={`num ${scoreClass(mask(tox.bidToxScore, bidWarmup))}`}>
              {fmtScore(mask(tox.bidToxScore, bidWarmup))}
            </td>
            <td className={`num ${fillRateClass(tox.bidFillRate)}`}>{fmtPct(tox.bidFillRate)}</td>
            <td className={`num ${ttfClass(tox.bidTtfMs)}`}>{fmtMs(tox.bidTtfMs)}</td>
          </tr>
          <tr>
            <td style={{ fontWeight: 600 }}>ASK</td>
            <td className={`num ${scoreClass(mask(tox.askMarkout5s, askWarmup))}`}>
              {fmt1(mask(tox.askMarkout5s, askWarmup))} bps
            </td>
            <td className={`num ${rateClass(mask(tox.askAdverseRate, askWarmup))}`}>
              {fmtPct(mask(tox.askAdverseRate, askWarmup))}
            </td>
            <td className={`num ${scoreClass(mask(tox.askToxScore, askWarmup))}`}>
              {fmtScore(mask(tox.askToxScore, askWarmup))}
            </td>
            <td className={`num ${fillRateClass(tox.askFillRate)}`}>{fmtPct(tox.askFillRate)}</td>
            <td className={`num ${ttfClass(tox.askTtfMs)}`}>{fmtMs(tox.askTtfMs)}</td>
          </tr>
        </tbody>
      </table>

      {/* TCA sparkline — rolling markout-5s trend, last ~8 minutes at
          bpt-analytics' ~2s cadence. Shows whether adverse selection is
          drifting (red trending down) or just noise around zero. Numeric
          value lives in the table above; this is for at-a-glance trend. */}
      <div
        style={{
          display: 'flex',
          gap: 16,
          padding: '6px 10px',
          borderTop: '1px solid var(--border)',
          alignItems: 'center',
          fontSize: 10,
          color: 'var(--text-muted)',
          fontFamily: 'var(--font-mono)',
        }}
      >
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <span style={{ color: 'var(--green)', fontWeight: 600 }}>BID</span>
          <MarkoutSparkline history={history} side="bid" />
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <span style={{ color: 'var(--red)', fontWeight: 600 }}>ASK</span>
          <MarkoutSparkline history={history} side="ask" />
        </div>
        <span style={{ marginLeft: 'auto', opacity: 0.7 }}>
          ±{Y_BOUND_BPS}bps · {history.length} samples
        </span>
      </div>
    </div>
  )
}

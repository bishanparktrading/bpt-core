import { useStore } from '../store'

// Options Pulse — renders bpt-radar's options-section MarketColor snapshot.
//
// One panel total, currently picking the first underlying it sees in the
// store's marketColor map. Multi-underlying split-by-tab is a follow-up
// once we publish more than one (BTC + ETH on Deribit). When the radar
// grows perp_/flow_/regime_ sections, this file can stay as-is — those
// will get their own sibling panels reading the same MarketColorMsg.

type Num = number | null | undefined
const nullish = (v: Num): v is null | undefined => v == null || isNaN(v as number)

const fmtVolPct = (v: Num) => (nullish(v) ? '—' : `${((v as number) * 100).toFixed(1)}%`)
const fmtVolPts = (v: Num) => (nullish(v) ? '—' : `${((v as number) * 100).toFixed(2)} vol pts`)
const fmtBps = (v: Num) => (nullish(v) ? '—' : ((v as number) * 10000).toFixed(1))
const fmtNum = (v: Num) => (nullish(v) ? '—' : (v as number).toLocaleString(undefined, { maximumFractionDigits: 0 }))
const fmtPx = (v: Num) =>
  nullish(v) ? '—' : (v as number).toLocaleString(undefined, { maximumFractionDigits: 2 })
const fmtSlope = (v: Num) => (nullish(v) ? '—' : (v as number).toFixed(3))

// YYYYMMDD → "2026-05-30 (14d)"
function formatExpiry(yyyymmdd: number, tteY: Num): string {
  if (!yyyymmdd) return '—'
  const s = String(yyyymmdd)
  const date = `${s.slice(0, 4)}-${s.slice(4, 6)}-${s.slice(6, 8)}`
  if (nullish(tteY)) return date
  const days = (tteY as number) * 365.25
  if (days < 1) return `${date} (${(days * 24).toFixed(0)}h)`
  return `${date} (${days.toFixed(0)}d)`
}

// GEX magnitude is dominated by gamma × OI scale — display in thousands for
// readability. Sign carries the regime signal: positive = vol-suppressing
// (dealer-long-gamma), negative = vol-amplifying.
function fmtGex(v: Num): string {
  if (nullish(v)) return '—'
  const n = v as number
  const k = n / 1000
  if (Math.abs(k) < 0.1) return n.toFixed(0)
  return `${k > 0 ? '+' : ''}${k.toFixed(1)}k`
}

function gexClass(v: Num): string {
  if (nullish(v)) return 'stat-value--muted'
  return (v as number) > 0 ? 'stat-value--green' : 'stat-value--red'
}

function termSpreadClass(v: Num): string {
  if (nullish(v)) return 'stat-value--muted'
  // Backwardation (negative term spread) signals stress — flag it.
  return (v as number) < 0 ? 'stat-value--red' : 'stat-value--green'
}

// 25Δ risk reversal: negative = market paying up for puts (bearish skew).
function rrClass(v: Num): string {
  if (nullish(v)) return 'stat-value--muted'
  const n = v as number
  if (Math.abs(n) < 0.01) return 'stat-value--muted'
  return n < 0 ? 'stat-value--red' : 'stat-value--green'
}

export function OptionsPulsePanel() {
  const marketColor = useStore((s) => s.marketColor)

  // Multi-underlying pick: deterministic — sort keys alphabetically and take
  // the first. Replace with tabs/selector when a second underlying lands.
  const entry = marketColor ? Object.entries(marketColor).sort(([a], [b]) => a.localeCompare(b))[0] : undefined

  if (!entry) {
    return (
      <div className="panel" style={{ gridArea: 'options-pulse' }}>
        <div className="panel-header">
          <span className="panel-title">Options Pulse</span>
          <span className="panel-badge">RADAR</span>
        </div>
        <div style={{ padding: '12px 16px', color: 'var(--text-muted)', fontSize: 13 }}>
          Waiting for radar data.
        </div>
      </div>
    )
  }

  const [underlying, msg] = entry
  const o = msg.options
  const oiCoverage = o.strikeCount > 0 ? o.strikesWithOi / o.strikeCount : 0

  return (
    <div className="panel" style={{ gridArea: 'options-pulse' }}>
      <div className="panel-header">
        <span className="panel-title">Options Pulse · {underlying}</span>
        <span className="panel-badge">
          {msg.exchange} · {o.strikeCount} strikes · {o.expiryCount} exp
        </span>
      </div>
      <div style={{ padding: '8px 16px', display: 'grid', gap: 8 }}>
        {/* Term-structure row */}
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 12, fontSize: 11 }}>
          <div>
            <div style={{ color: 'var(--text-muted)' }}>Front</div>
            <div style={{ fontWeight: 600 }}>{formatExpiry(o.frontExpiry, o.frontTimeToExpiryY)}</div>
            <div>ATM IV: <span className="stat-value">{fmtVolPct(o.frontAtmIv)}</span></div>
          </div>
          <div>
            <div style={{ color: 'var(--text-muted)' }}>Back</div>
            <div style={{ fontWeight: 600 }}>{formatExpiry(o.backExpiry, o.backTimeToExpiryY)}</div>
            <div>ATM IV: <span className="stat-value">{fmtVolPct(o.backAtmIv)}</span></div>
          </div>
          <div>
            <div style={{ color: 'var(--text-muted)' }}>Term spread</div>
            <div className={`stat-value ${termSpreadClass(o.termSpread)}`} style={{ fontSize: 16, fontWeight: 700 }}>
              {fmtVolPts(o.termSpread)}
            </div>
            <div style={{ color: 'var(--text-muted)', fontSize: 10 }}>
              {nullish(o.termSpread) ? '' : (o.termSpread as number) < 0 ? 'backwardation' : 'contango'}
            </div>
          </div>
        </div>

        {/* Skew row */}
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, fontSize: 11 }}>
          <div>
            <div style={{ color: 'var(--text-muted)' }}>25Δ Risk Reversal (front)</div>
            <div className={`stat-value ${rrClass(o.frontRr25d)}`} style={{ fontSize: 14, fontWeight: 600 }}>
              {fmtBps(o.frontRr25d)} bps
            </div>
          </div>
          <div>
            <div style={{ color: 'var(--text-muted)' }}>ATM skew dIV/d(logK)</div>
            <div className="stat-value" style={{ fontSize: 14, fontWeight: 600 }}>
              {fmtSlope(o.frontSkewSlope)}
            </div>
          </div>
        </div>

        {/* OI-derived row */}
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 12, fontSize: 11 }}>
          <div>
            <div style={{ color: 'var(--text-muted)' }}>GEX (Σ ±γ·OI)</div>
            <div className={`stat-value ${gexClass(o.gex)}`} style={{ fontSize: 16, fontWeight: 700 }}>
              {fmtGex(o.gex)}
            </div>
            <div style={{ color: 'var(--text-muted)', fontSize: 10 }}>
              {nullish(o.gex) ? 'no OI yet' : (o.gex as number) > 0 ? 'vol-suppressing' : 'vol-amplifying'}
            </div>
          </div>
          <div>
            <div style={{ color: 'var(--text-muted)' }}>Max pain (front)</div>
            <div className="stat-value" style={{ fontSize: 14, fontWeight: 600 }}>
              {fmtPx(o.maxPainStrike)}
            </div>
            <div style={{ color: 'var(--text-muted)', fontSize: 10 }}>
              fwd: {fmtPx(o.frontForwardPrice)}
            </div>
          </div>
          <div>
            <div style={{ color: 'var(--text-muted)' }}>Total OI</div>
            <div className="stat-value" style={{ fontSize: 14, fontWeight: 600 }}>
              {fmtNum(o.totalOi)}
            </div>
            <div style={{ color: 'var(--text-muted)', fontSize: 10 }}>
              OI coverage: {(oiCoverage * 100).toFixed(0)}%
            </div>
          </div>
        </div>
      </div>
    </div>
  )
}

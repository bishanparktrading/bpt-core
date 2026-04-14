import { useStore } from '../store'
import type { Fill } from './Blotter'

interface RiskPanelProps {
  // Optional overrides for the archive view.
  fills?: Fill[]
  startingCapital?: number
  // When rendering an archive run we have these pre-computed from summary.json
  // and don't need to recompute client-side.
  precomputed?: {
    totalPnl: number
    returnPct: number
    maxDdPct: number
    sharpe: number
    winRate: number
    totalFills: number
  }
}

export function RiskPanel(props: RiskPanelProps = {}) {
  // Two modes — see EquityChart for the same split:
  //   1. Live/paper — Total PnL / Return / Max DD come from the heimdall
  //      AccountSnapshot history in the store. Sharpe / Win Rate / Fills
  //      still come from the local fill stream.
  //   2. Archive — props.fills + props.startingCapital + optional precomputed.
  const liveFills = useStore((s) => s.fills)
  const accountHistory = useStore((s) => s.accountHistory)
  const isArchive = props.fills !== undefined
  const fills = props.fills ?? liveFills
  const startingCapital = props.startingCapital ?? 0

  let totalPnl: number
  let returnPct: number
  let maxDdPct: number
  let sharpe: number
  let winRate: number
  let totalFills: number

  if (props.precomputed) {
    totalPnl   = props.precomputed.totalPnl
    returnPct  = props.precomputed.returnPct
    maxDdPct   = props.precomputed.maxDdPct
    sharpe     = props.precomputed.sharpe
    winRate    = props.precomputed.winRate
    totalFills = props.precomputed.totalFills
  } else if (isArchive) {
    totalPnl  = fills.length ? fills[fills.length - 1].equity - startingCapital : 0
    returnPct = startingCapital ? (totalPnl / startingCapital) * 100 : 0

    let peak = startingCapital
    let maxDd = 0
    for (const f of fills) {
      if (f.equity > peak) peak = f.equity
      const dd = (peak - f.equity) / peak
      if (dd > maxDd) maxDd = dd
    }
    maxDdPct = maxDd * 100

    const realised = fills.filter((f) => f.realizedPnl !== 0).map((f) => f.realizedPnl)
    const mean = realised.length ? realised.reduce((a, b) => a + b, 0) / realised.length : 0
    const variance = realised.length
      ? realised.reduce((a, b) => a + (b - mean) ** 2, 0) / realised.length
      : 0
    sharpe = variance > 0 ? mean / Math.sqrt(variance) : 0

    const wins = realised.filter((p) => p > 0).length
    winRate = realised.length ? (wins / realised.length) * 100 : 0
    totalFills = fills.length
  } else {
    // Live mode — anchor on the first observed account equity; deltas from
    // there give Total PnL / Return / Max DD. Sharpe and Win Rate still come
    // from the fill stream because account snapshots don't carry per-trade
    // realised PnL.
    const first = accountHistory[0]?.equity ?? 0
    const last = accountHistory[accountHistory.length - 1]?.equity ?? first
    totalPnl  = first ? last - first : 0
    returnPct = first ? (totalPnl / first) * 100 : 0

    let peak = first
    let maxDd = 0
    for (const a of accountHistory) {
      if (a.equity > peak) peak = a.equity
      if (peak > 0) {
        const dd = (peak - a.equity) / peak
        if (dd > maxDd) maxDd = dd
      }
    }
    maxDdPct = maxDd * 100

    const realised = liveFills.filter((f) => f.realizedPnl !== 0).map((f) => f.realizedPnl)
    const mean = realised.length ? realised.reduce((a, b) => a + b, 0) / realised.length : 0
    const variance = realised.length
      ? realised.reduce((a, b) => a + (b - mean) ** 2, 0) / realised.length
      : 0
    sharpe = variance > 0 ? mean / Math.sqrt(variance) : 0

    const wins = realised.filter((p) => p > 0).length
    winRate = realised.length ? (wins / realised.length) * 100 : 0
    totalFills = liveFills.length
  }

  const pnlColour =
    totalPnl > 0 ? 'stat-value--green' : totalPnl < 0 ? 'stat-value--red' : 'stat-value--muted'
  const retColour =
    returnPct > 0 ? 'stat-value--green' : returnPct < 0 ? 'stat-value--red' : 'stat-value--muted'

  return (
    <div className="panel">
      <div className="panel-header">
        <span className="panel-title">Risk</span>
      </div>
      <div className="stat-grid">
        <div className="stat-cell">
          <span className="stat-label">Total PnL</span>
          <span className={`stat-value ${pnlColour}`}>
            {totalPnl >= 0 ? '+' : ''}${totalPnl.toFixed(2)}
          </span>
        </div>
        <div className="stat-cell">
          <span className="stat-label">Return</span>
          <span className={`stat-value ${retColour}`}>
            {returnPct >= 0 ? '+' : ''}
            {returnPct.toFixed(2)}%
          </span>
        </div>
        <div className="stat-cell">
          <span className="stat-label">Max DD</span>
          <span className="stat-value stat-value--red stat-value--sm">
            -{maxDdPct.toFixed(2)}%
          </span>
        </div>
        <div className="stat-cell">
          <span className="stat-label">Sharpe</span>
          <span className="stat-value stat-value--sm">{sharpe.toFixed(2)}</span>
        </div>
        <div className="stat-cell">
          <span className="stat-label">Win Rate</span>
          <span className="stat-value stat-value--sm">{winRate.toFixed(1)}%</span>
        </div>
        <div className="stat-cell">
          <span className="stat-label">Fills</span>
          <span className="stat-value stat-value--sm stat-value--muted">{totalFills}</span>
        </div>
      </div>
    </div>
  )
}

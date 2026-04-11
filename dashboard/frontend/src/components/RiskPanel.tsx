import { useStore } from '../store'

export function RiskPanel() {
  const fills = useStore((s) => s.fills)
  const startingCapital = useStore((s) => s.startingCapital)

  // All risk metrics are derived from the fill stream, not pushed from the
  // bridge — this keeps the bridge's responsibility surface small.
  const totalPnl = fills.length ? fills[fills.length - 1].equity - startingCapital : 0
  const returnPct = startingCapital ? (totalPnl / startingCapital) * 100 : 0

  // Peak-to-trough drawdown on the equity curve
  let peak = startingCapital
  let maxDd = 0
  for (const f of fills) {
    if (f.equity > peak) peak = f.equity
    const dd = (peak - f.equity) / peak
    if (dd > maxDd) maxDd = dd
  }
  const maxDdPct = maxDd * 100

  // Realised PnL per fill → simple Sharpe estimate
  const realised = fills.filter((f) => f.realizedPnl !== 0).map((f) => f.realizedPnl)
  const mean = realised.length ? realised.reduce((a, b) => a + b, 0) / realised.length : 0
  const variance = realised.length
    ? realised.reduce((a, b) => a + (b - mean) ** 2, 0) / realised.length
    : 0
  const std = Math.sqrt(variance)
  const sharpe = std > 0 ? mean / std : 0

  // Win rate on the realised-PnL fills (the closing leg of each round trip)
  const wins = realised.filter((p) => p > 0).length
  const winRate = realised.length ? (wins / realised.length) * 100 : 0

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
          <span className="stat-value stat-value--sm stat-value--muted">{fills.length}</span>
        </div>
      </div>
    </div>
  )
}

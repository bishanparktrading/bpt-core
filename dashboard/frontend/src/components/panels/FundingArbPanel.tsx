import type { FundingArbStrategyState } from '../../types/messages'

const fmt = (v: number, d = 2) => v.toFixed(d)
const fmtSigned = (v: number, d = 6) => (v >= 0 ? '+' : '') + v.toFixed(d)
const fmtBps = (v: number) => v.toFixed(1)
const fmtPct = (v: number) => (v * 100).toFixed(3) + '%'

const LEG_LABEL: Record<FundingArbStrategyState['legStatus'], string> = {
  flat: 'FLAT',
  entering: 'ENTERING',
  open: 'OPEN',
  exiting: 'EXITING',
}

const LEG_COLOUR: Record<FundingArbStrategyState['legStatus'], string> = {
  flat: 'stat-value--muted',
  entering: 'limit-warn',
  open: 'stat-value--green',
  exiting: 'limit-warn',
}

// Colours that match the spot/perp series in DualLegChart so the row
// labels in this panel read as legends for the main chart.
const SPOT_COLOUR = '#5c9ce6'
const PERP_COLOUR = '#e89436'

export function FundingArbPanel({ state: ss }: { state: FundingArbStrategyState }) {
  const deltaClass =
    Math.abs(ss.hedgedDelta) < 1e-4
      ? 'stat-value--green'
      : Math.abs(ss.hedgedDelta) < 1e-2
        ? 'limit-warn'
        : 'stat-value--red'

  return (
    <div className="panel" style={{ gridArea: 'stratstate' }}>
      <div className="panel-header">
        <span className="panel-title">Strategy State</span>
        <span className="panel-badge">
          FundingArb · <span className={LEG_COLOUR[ss.legStatus]}>{LEG_LABEL[ss.legStatus]}</span>
        </span>
      </div>

      <table className="blotter-table" style={{ fontSize: 11 }}>
        <thead>
          <tr>
            <th colSpan={2} style={{ textAlign: 'left' }}>
              Legs
            </th>
            <th colSpan={2} style={{ textAlign: 'left' }}>
              Signal
            </th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td style={{ color: SPOT_COLOUR, width: 55 }}>SPOT</td>
            <td className="num">
              {fmt(ss.spotPx, 4)} <span className="stat-value--muted">× {fmtSigned(ss.spotQty)}</span>
            </td>
            <td style={{ color: 'var(--text-muted)', width: 60 }}>FUND</td>
            <td className="num">{fmtPct(ss.fundingRate)}</td>
          </tr>
          <tr>
            <td style={{ color: PERP_COLOUR }}>PERP</td>
            <td className="num">
              {fmt(ss.perpPx, 4)} <span className="stat-value--muted">× {fmtSigned(ss.perpQty)}</span>
            </td>
            <td style={{ color: 'var(--text-muted)' }}>FUND-APR</td>
            <td className="num">{fmt(ss.fundingApr * 100, 2)}%</td>
          </tr>
          <tr>
            <td style={{ color: 'var(--text-muted)' }}>BASIS</td>
            <td
              className={`num ${ss.basisBps > 0.5 ? 'stat-value--green' : ss.basisBps < -0.5 ? 'stat-value--red' : 'stat-value--muted'}`}
            >
              {fmtBps(ss.basisBps)} bps
            </td>
            <td style={{ color: 'var(--text-muted)' }}>DELTA</td>
            <td className={`num ${deltaClass}`}>{fmtSigned(ss.hedgedDelta)}</td>
          </tr>
        </tbody>
      </table>
    </div>
  )
}

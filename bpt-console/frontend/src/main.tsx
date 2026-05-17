import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import App from './App.tsx'
import ArchiveApp from './ArchiveApp.tsx'
import RadarApp from './RadarApp.tsx'

// Hard split between the live trading console, the research archive, and the
// market-color radar. Each lives at its own URL because they answer different
// questions:
//   /         → live trading: P&L, fills, strategy state, order book
//   /archive  → research: completed backtest runs
//   /radar    → market color: options IV/skew/GEX, future perp/flow/regime
// No cross-navigation — the views don't share concerns.
const path = window.location.pathname

function pick() {
  if (path.startsWith('/archive')) return <ArchiveApp />
  if (path.startsWith('/radar')) return <RadarApp />
  return <App />
}

createRoot(document.getElementById('root')!).render(<StrictMode>{pick()}</StrictMode>)

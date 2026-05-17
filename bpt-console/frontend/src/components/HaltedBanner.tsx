import { useStore } from '../store'

// Persistent strip that takes over the top of the shell whenever the
// connection status is 'halted'.  The purpose is to make the halted
// state impossible to miss even if the trader has looked away — the
// status dot in the top bar alone isn't enough.
export function HaltedBanner() {
  const status = useStore((s) => s.status)
  if (status !== 'halted') return null

  return (
    <div className="halted-banner" role="alert">
      <span className="halted-banner__dot" />
      <span className="halted-banner__label">TRADING HALTED</span>
      <span className="halted-banner__hint">
        Strategy is blocked from emitting new orders. Use RESUME when ready.
      </span>
    </div>
  )
}

# dashboard/frontend

Vite + React + TypeScript single-bundle app. **Three routes, three separate top-level apps** — chosen because the three surfaces answer different questions and shouldn't share chrome.

| Route | Top-level component | Surface | Backend dependency |
|---|---|---|---|
| `/` | `App.tsx` | Live trading console — P&L, fills, equity curve, strategy state, order book, holdings | `bpt-bridge` WS at `:8080` (live trading streams) |
| `/archive` | `ArchiveApp.tsx` | Backtest research — list, detail, diff, sweep | Vite dev-server middleware reads `bpt-backtester/results/` |
| `/radar` | `RadarApp.tsx` | Market color — options IV / RR / skew / GEX / max-pain; future perp basis, flow, regime, calendar | `bpt-bridge` WS at `:8080` (radar `marketColor` frames) |

`main.tsx` picks based on `window.location.pathname.startsWith(...)`. **No cross-navigation between routes** — each is opened deliberately because the views serve different audiences (trader vs. researcher vs. analyst).

## Why this split

- **`/` (live trading)** is an execution surface. Update cadence: ms. The whole layout is shaped around the running strategy — strategy state panel, equity curve, order book. Operators stare at it during a session and shouldn't be one click from leaving.
- **`/archive`** is a research surface. Update cadence: hours. Reads completed backtest run artifacts; no live connection. Lives behind a deliberate URL so a trader can't accidentally navigate away from the live view.
- **`/radar`** is an analytics surface. Update cadence: seconds. Carries forward-looking signals (IV term structure, GEX regime, skew) that inform — but don't drive — execution. Distinct from the trading console because the audience is different (analyst / option strategist, not operator) and the chrome is different (no trading-mode pill, no order book).

## Shared infrastructure

All three apps share:

- `store.ts` — single zustand store. Each route subscribes to whichever slices it cares about; unused fields don't render anywhere.
- `ws/client.ts` — single WebSocket client to the bridge. `VALID_TYPES` set gates incoming message types; add a string here when introducing a new server-pushed message kind, or it'll be dropped at the client with `[ws] unknown message type` in the console.
- `types/messages.ts` — every WS message shape. The `Msg` discriminated-union type drives the store's `handleMessage` switch.
- `index.css` — `.shell` (trading), `.archive-*` (archive), `.radar-*` (radar) grids. Each route owns its CSS; no cross-contamination.

## Where to put new things

| You want to | Put it in |
|---|---|
| New live-trading panel (fills, risk, strategy-specific) | `components/`, render in `App.tsx` shell grid |
| New strategy panel for a new strategy kind | `components/panels/`, register in `panels/index.ts` |
| New market-color domain (perp basis, flow, regime) | `components/` (or `components/radar/`), render in `RadarApp.tsx` |
| New WS message from the bridge | (1) add interface in `types/messages.ts` and append to `Msg`; (2) add string to `VALID_TYPES` in `ws/client.ts`; (3) add case in `store.ts`'s `handleMessage` |
| New URL route | Top-level app in `<NameApp>.tsx` + branch in `main.tsx` |

## Development

```bash
cd dashboard/frontend
npm install
VITE_WS_URL=ws://localhost:8080 npm run dev   # http://localhost:5173/
```

Open routes directly: `http://localhost:5173/`, `http://localhost:5173/archive`, `http://localhost:5173/radar`.

The bridge (`bpt-bridge`) must be running for `/` and `/radar` to populate. See `deploy/generate-units-dev.sh` and `bpt-dev-stack.target` / `bpt-dev-radar-stack.target` for the backend.

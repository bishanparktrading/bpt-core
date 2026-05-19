# bpt-backtester

Deterministic single-process backtest harness. Strategy code + matching
engine + tape replay all run in one thread — no Aeron, no IPC, no
scheduler jitter. Same input always produces the same output, which is
what parameter sweeps need.

The legacy multi-process backtester (Aeron-driven, Arrow/Parquet tape
loader) was retired 2026-05-20 — live testnet trading covers the
integration-smoke role at higher fidelity, and the parameter-tuning
role moved entirely to this single binary.

See [service-anatomy.md](../docs/service-anatomy.md) for the canonical
service shape.

## At a glance

```mermaid
%%{init: {
  'theme': 'base',
  'themeVariables': {
    'fontFamily': '"SF Mono", "JetBrains Mono", "Cascadia Code", Consolas, monospace',
    'fontSize': '14px',
    'lineColor': '#475569',
    'primaryColor': '#1e293b',
    'primaryTextColor': '#f8fafc',
    'primaryBorderColor': '#0f172a'
  }
}}%%
flowchart TD
    tape["<b>TAPE FILES</b><br/>.wslog (from bpt-tape)"]

    subgraph bt["bpt-backtester (single binary, single thread)"]
        harness["<b>StrategyHarness</b><br/>reads tape, drives one event at a time"]
        decoders["MdDecoders<br/>(reused from md-gateway)"]
        harness_pub["HarnessMdPublisher<br/>(in-process — no Aeron,<br/>no encode/decode)"]
        strategy["<b>IStrategy</b><br/>(same code as live)"]
        ogw_client["InProcessOrderGatewayClient"]
        match_eng["MatchingEngine"]
        results["fills · final equity log"]

        harness --> decoders
        decoders --> harness_pub
        harness_pub -->|"MdBbo / Trade / Book"| strategy
        strategy -->|"NewOrder"| ogw_client
        ogw_client --> match_eng
        match_eng -->|"fill"| strategy
        match_eng --> results
    end

    tape --> harness

    classDef external fill:#fff3cd,stroke:#856404,color:#000
    classDef domain fill:#dbeafe,stroke:#1e40af,stroke-width:2px,color:#000
    classDef layer fill:#f5f5f5,stroke:#333,color:#000
    class tape external
    class harness,strategy,match_eng domain
    class decoders,harness_pub,ogw_client,results layer
```

## Layers

The harness is a small library that wires strategy + matching engine +
tape replay end-to-end.

| Layer | Notes |
|---|---|
| Composition root | `src/main.cpp` — CLI parsing, opts → harness |
| Harness | `harness/strategy_harness.{h,cpp}` — drives replay |
| In-process clients | `harness/inprocess_order_gateway_client.{h,cpp}`, `strategy/md/inprocess_md_client.h`, `strategy/refdata/inprocess_refdata_client.h` |
| MdPublisher (in-process) | `harness/harness_md_publisher.h` |
| Tape reader | `harness/wslog_reader.h` |
| Matching engine | `matching/matching_engine.{h,cpp}` |
| Results | `results/results_collector.{h,cpp}` |
| Latency model | `latency/latency_model.h` |

## Reading order

1. `src/main.cpp`
2. `harness/strategy_harness.{h,cpp}` — the in-process driver.
3. `harness/inprocess_order_gateway_client.{h,cpp}` — strategy's order
   client without Aeron.
4. `harness/harness_md_publisher.h` — replaces the live `MdPublisher` chain.
5. `matching/matching_engine.{h,cpp}` — per-instrument order book + fill logic.

## Build

```bash
bazel build //bpt-backtester:bpt-backtester
bazel test  //bpt-backtester:backtester_unit_tests
```

## Run

```bash
bazel-bin/bpt-backtester/bpt-backtester \
  --strategy-config       bpt-strategy/config/avellaneda_stoikov.qa-hyperliquid.toml \
  --instrument-mapping    config/instruments/instrument_mapping.hyperliquid.json \
  --wslog                 /path/to/tape1.wslog /path/to/tape2.wslog \
  --output-dir            results \
  --starting-capital      1000 \
  --strategy-name         AvellanedaStoikov
```

Output: per-run directory under `--output-dir` containing `summary.json`,
`trades.csv`, `pnl_curve.csv`. The run-id is composed from
`--strategy-name`, `--params-hash`, `--git-sha`, and the start/end
timestamps in the tape.

## Status

HL backtester end-to-end (`.wslog` from bpt-tape's HL capture) is the
proven path — see `project_hl_backtester_support` in repo memory. Other
venues land as bpt-tape adds capture coverage for them.

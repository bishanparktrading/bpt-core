# bpt-md-gateway

Market-data gateway: subscribes to exchange WebSocket feeds (Binance / OKX /
Deribit / Hyperliquid), normalises into SBE messages, publishes on Aeron to
strategy + pricer + analytics + bridge + radar.

See [service-anatomy.md](../docs/service-anatomy.md) for the canonical
layered shape every bpt-* service follows. This README shows the specific
shape this service has.

## At a glance

```mermaid
%%{init: {
  'theme': 'base',
  'flowchart': {
    'curve': 'linear'
  },
  'themeVariables': {
    'fontFamily': '"IBM Plex Sans", "Inter", "Helvetica Neue", Arial, sans-serif',
    'fontSize': '14px'
  }
}}%%
flowchart LR
    %% ── Left swimlane: external producers + control ──────────────────────
    subgraph EXT["External"]
        direction TB
        venue("Exchange<br/>Binance · OKX<br/>Deribit · Hyperliquid")
        strat_in("strategy<br/>MdSubscribeBatch")
    end

    %% ── Middle swimlane: bpt-md-gateway ──────────────────────────────────
    subgraph MDGW["bpt-md-gateway"]
        direction TB

        subgraph adapter["per-venue Adapter"]
            direction TB
            ws("*MdWsClient")
            decoder("*MdDecoder&lt;Pub&gt;")
            md_pub("MdPublisher<br/>validate · breaker<br/>SBE encode (zero-copy)")
        end

        ctrl_sub("MdControlSubscriber")
        sub_mgr("SubscriptionManager")

        slow("slow-path publishers<br/>FundingRate · InstrumentStats · Ack")
    end

    %% ── Right swimlane: Aeron + consumers ────────────────────────────────
    subgraph RIGHT["Aeron + Consumers"]
        direction TB
        aeron_box("Aeron<br/>shared-memory<br/>log buffers")
        consumers("strategy · pricer<br/>analytics · bridge · radar")
    end

    %% Hot tick path (edges 0–4)
    venue --> ws
    ws --> decoder
    decoder --> md_pub
    md_pub --> aeron_box
    aeron_box --> consumers

    %% Slow path (edges 5–6)
    decoder --> slow
    slow --> aeron_box

    %% Control / ack path (edges 7–10)
    strat_in --> ctrl_sub
    ctrl_sub --> sub_mgr
    sub_mgr --> ws
    sub_mgr --> slow

    %% Edge colours carry the semantic load (no edge labels — see legend below)
    linkStyle 0,1,2,3,4 stroke:#ef4444,stroke-width:2.5px,fill:none
    linkStyle 5,6 stroke:#10b981,stroke-width:2.5px,fill:none
    linkStyle 7,8,9,10 stroke:#22d3ee,stroke-width:2.5px,fill:none

    %% Off-white "data card" stadium nodes + dark slate swimlanes (trading-terminal palette)
    classDef node fill:#f1f5f9,stroke:#cbd5e1,stroke-width:1.5px,color:#0f172a
    class venue,strat_in,ws,decoder,md_pub,ctrl_sub,sub_mgr,slow,aeron_box,consumers node

    style EXT fill:#0f172a,stroke:#475569,stroke-width:1px,color:#e2e8f0
    style MDGW fill:#1e1b4b,stroke:#4338ca,stroke-width:1px,color:#c7d2fe
    style RIGHT fill:#0f172a,stroke:#475569,stroke-width:1px,color:#e2e8f0
    style adapter fill:#312e81,stroke:#6366f1,stroke-width:1px,color:#e0e7ff
```

**Legend**

| Edge colour | Path | What flows |
|---|---|---|
| 🟥 **Red** | Hot tick path | BBO / Trade / OrderBook (zero-copy SBE into Aeron log buffer) |
| 🟩 **Green** | Slow path | FundingRate / InstrumentStats / Ack (codec + offer) |
| 🟦 **Blue** | Control | MdSubscribeBatch from strategy → SubscriptionManager → adapter / slow-pub |

## Detailed data flow (every major object)

Every component named, every arrow labelled. `==>` is the hot tick path,
`-->` is slow path or control, `-.->` is configuration / composition wiring.

```mermaid
%%{init: {
  'theme': 'base',
  'flowchart': {
    'curve': 'linear'
  },
  'themeVariables': {
    'fontFamily': '"IBM Plex Sans", "Inter", "Helvetica Neue", Arial, sans-serif',
    'fontSize': '13px'
  }
}}%%
flowchart TD
    venue("📡 <b>Exchange</b><br/>(Binance / OKX / Deribit / HL)")
    strat_in("<b>strategy</b><br/>publishes MdSubscribeBatch")
    consumers("<b>strategy · pricer · analytics<br/>bridge · radar</b><br/>consume MD via Aeron")
    aeron[("☕ <b>Aeron MediaDriver</b><br/>shared-memory log buffers")]

    subgraph mdgw["bpt-md-gateway"]
        direction TB

        main("<b>main.cpp</b><br/>composition root")
        service("<b>MdGatewayService</b><br/>IService · main poll loop")
        factory("<b>MdGatewayAeronBus::build</b><br/>factory")

        subgraph bus["MdGatewayBus (struct)"]
            ctrl_sub("control_sub<br/><i>api::MdControlSubscriber</i>")
            ack_pub("ack_pub<br/><i>api::AckPublisher</i>")
            funding_pub("funding_pub<br/><i>api::FundingRatePublisher</i>")
            stats_pub("stats_pub<br/><i>api::InstrumentStatsPublisher</i>")
        end

        sub_mgr("<b>SubscriptionManager</b><br/>canonical_id → per-venue<br/>holds SubscriptionMap")

        subgraph adapter["BinanceMdAdapter : AdapterBase&lt;Pub&gt; : IAdapter (one per venue)"]
            direction TB
            ws_client("<b>BinanceMdWsClient</b><br/>Boost.Beast WS / TLS<br/>(IO thread)")
            queue("<b>SpscQueue&lt;Frame&gt;</b><br/>IO → publisher thread")
            decoder("<b>BinanceMdDecoder&lt;Pub&gt;</b><br/>simdjson → domain<br/>(publisher thread)")
            md_pub("<b>MdPublisher</b><br/>owns MdValidator + ValidationDropBreaker<br/>+ aeron::Publisher; one per adapter,<br/>publisher-thread-confined")
            encoder("<b>BinanceMdEncoder</b><br/>build_streams_query")
        end

        subgraph codecs["codecs/"]
            sbe_funding("<b>SbeFundingRateCodec</b><br/>encode(fr, scratch) → span")
            sbe_stats("<b>SbeInstrumentStatsCodec</b>")
            sbe_ack("<b>SbeMdSubscription*Codec</b><br/>(3 message types)")
        end

        common_pub("<b>bpt::common::aeron::Publisher</b><br/>thin wrapper: offer + back-pressure")

        main --> service
        main -.builds.-> factory
        factory -.constructs.-> bus
        service -.owns.-> bus
        service -.owns.-> sub_mgr
        service -.owns.-> adapter

        venue ==>|"1: WS frame (JSON)"| ws_client
        ws_client ==>|"2: push raw frame"| queue
        queue ==>|"3: pop on pub thread"| decoder
        decoder ==>|"4: MdBbo / MdTrade /<br/>MdOrderBook"| md_pub
        md_pub ==>|"5: validate · breaker ·<br/>SBE encode + tryClaim (zero-copy)"| aeron

        decoder -->|"funding_cb<br/>(std::function)"| funding_pub
        decoder -->|"stats_cb"| stats_pub
        funding_pub -->|"encode(fr, scratch)"| sbe_funding
        stats_pub --> sbe_stats
        sbe_funding -->|"span&lt;byte&gt;"| common_pub
        sbe_stats --> common_pub

        aeron ==>|"a: MdSubscribeBatch"| ctrl_sub
        ctrl_sub ==>|"b: decoded batch"| service
        service ==>|"c: apply_batch(...)"| sub_mgr
        sub_mgr -->|"d: ack via SBE"| ack_pub
        ack_pub --> sbe_ack
        sub_mgr ==>|"e: subscribe(symbol)"| adapter
        adapter --> encoder
        encoder -->|"f: subscribe URL"| ws_client
        ws_client -.->|"g: WS sub frame"| venue

        common_pub ==>|"offer (back-pressure policy)"| aeron
        sbe_ack --> common_pub
    end

    aeron ==>|"deliver via shared memory"| consumers
    strat_in --> aeron

    %% Edge palette — same scheme as the at-a-glance diagram above
    %%   composition / lifetime (edges 0–5) — slate
    %%   hot tick path           (edges 6–10, 28) — red
    %%   slow path               (edges 11–16, 26–27) — emerald
    %%   control / ack           (edges 17–25, 29) — cyan
    linkStyle 0,1,2,3,4,5 stroke:#64748b,stroke-width:1.5px,fill:none
    linkStyle 6,7,8,9,10,28 stroke:#ef4444,stroke-width:2.5px,fill:none
    linkStyle 11,12,13,14,15,16,26,27 stroke:#10b981,stroke-width:2px,fill:none
    linkStyle 17,18,19,20,21,22,23,24,25,29 stroke:#22d3ee,stroke-width:2px,fill:none

    %% Off-white "data card" stadium nodes + dark slate/indigo swimlanes (trading-terminal palette)
    classDef node fill:#f1f5f9,stroke:#cbd5e1,stroke-width:1.5px,color:#0f172a
    class venue,strat_in,consumers,aeron,md_pub,decoder,sub_mgr,ctrl_sub,ack_pub,funding_pub,stats_pub,ws_client,queue,encoder,sbe_funding,sbe_stats,sbe_ack,common_pub,main,service,factory node

    style mdgw fill:#1e1b4b,stroke:#4338ca,stroke-width:2px,color:#c7d2fe
    style bus fill:#0f172a,stroke:#475569,stroke-width:1px,color:#e2e8f0
    style adapter fill:#312e81,stroke:#6366f1,stroke-width:1px,color:#e0e7ff
    style codecs fill:#0f172a,stroke:#475569,stroke-width:1px,color:#e2e8f0
```

**Legend** (same colour scheme as the at-a-glance above)

| Edge colour | Path | Notes |
|---|---|---|
| 🟥 **Red** | Hot tick path (numbered 1–5) | Zero vtable, zero-copy at the Aeron boundary. ~µs. `MdPublisher` does validate + drop-rate breaker + SBE encode + offer in one step. |
| 🟩 **Green** | Slow path | Funding rate, instrument stats, ack. Same Aeron at the end, but goes through a `Codec` + `Publisher::offer` with a stack scratch buffer. ~µs per call, lower frequency. |
| 🟦 **Cyan** | Control / ack (lettered a–g) | `MdSubscribeBatch` from strategy → `MdControlSubscriber` → `MdGatewayService` → `SubscriptionManager` → ack via SBE + adapter subscribe. |
| ⬜ **Slate** | Composition / lifetime | One-shot ownership wiring — main builds factory, factory constructs bus, service owns components. |

**The control path (lettered a–g):** strategy publishes `MdSubscribeBatch` → flows through Aeron → `MdControlSubscriber` decodes → `MdGatewayService` routes via `SubscriptionManager` → which both (d) publishes an ack and (e) tells the venue adapter to subscribe → which builds the URL and hands to the WS client → which reconnects with the new subscriptions baked into the URL (Binance) or sends a runtime subscribe frame (OKX / Deribit / HL).

## Streams produced

| Stream | ID | Contents | Cadence |
|---|---|---|---|
| `md_data` | 2002 | MdBbo, MdTrade, MdOrderBook (SBE) | kHz per active instrument |
| `md_ack_hb` | 2003 | MdSubscriptionAck, MdSubscriptionHeartbeat, MdServiceHeartbeat | Hz |
| `funding_rate` | 1005 | FundingRate updates | ~Hz per perp instrument |
| `instrument_stats` | 2004 | InstrumentStats (OI, mark, index, last, 24h vol) | per-instrument ~10s |

## Streams consumed

| Stream | ID | Contents |
|---|---|---|
| `md_control` | 2001 | MdSubscribeBatch from strategy (the only inbound) |

## Layers (canonical shape)

| Layer | Code location | Files |
|---|---|---|
| Composition root | `src/main.cpp` | — |
| Service | `app/md_gateway_service.{h,cpp}` | `MdGatewayService` (IService impl) |
| Bus | `messaging/aeron_bus.{h,cpp}` | `MdGatewayBus` struct + `build()` factory |
| Routing | `subscription/subscription_manager.{h,cpp}` | `SubscriptionManager` |
| Adapter | `adapter/<venue>/<venue>_md_adapter.{h,cpp}` | 4 adapters, share `adapter/common/adapter_base.h` |
| Wire | `adapter/<venue>/<venue>_md_ws_client.{h,cpp}` | Boost.Beast + Asio WS |
| External codec | `adapter/<venue>/<venue>_md_decoder.h` (header-only template), `<venue>_md_encoder.{h,cpp}` | simdjson decoder, free-function encoder |
| Pub/Sub (slow) | `messaging/publishers/{api,aeron}/...`, `messaging/subscribers/{api,aeron}/...` | api/aeron split |
| Pub (hot) | `messaging/publishers/md_publisher.{h,cpp}` | `MdPublisher` (templated chain target) |
| Internal codec | `messaging/codecs/sbe_*.{h,cpp}` | All satisfy `Codec<C, T>` |
| Hot-path support | `md/{md_encoder,md_validator,md_types,md_publisher_concept,validation_drop_breaker,...}.h` | template chain + MdPublisher-owned validator/breaker pieces |

## Concepts

| Concept | Where defined | Used by |
|---|---|---|
| `MdSink<P>` | `md/md_publisher_concept.h` | All 4 venue decoders constrain their `Pub` template param |
| `MdPublisher<P>` | `md/md_publisher_concept.h` | Prod `MdPublisher` self-verifies via `static_assert` |
| `Codec<C, T>` | `bpt-common/include/bpt_common/codec/codec.h` | All slow-path SBE codecs verify via `static_assert` |

## Test seams

- **Component tests**: `tests/component/test_<venue>_adapter.cpp` — captured JSON fragments → expected SBE output. Uses `FakeMdPublisher` (satisfies `MdSink` concept without inheriting any port).
- **Unit tests**: `tests/unit/test_*.cpp` — codec round-trips, validator, drop-breaker, subscription manager.
- **Component test fake for AckPublisher**: `tests/component/fake_ack_publisher.h` — inherits `api::AckPublisher` port.

## Hot path vs slow path summary

| | Hot path | Slow path |
|---|---|---|
| What | MD ticks (BBO/Trade/OrderBook) | Funding rate, instrument stats, acks, heartbeats |
| Rate | kHz per instrument | Hz to 0.01 Hz |
| Dispatch | Template composition + concept (`MdSink`) | Virtual port (api/aeron split) |
| Encode | Zero-copy SBE via `MdPublisher::tryClaim` | `Codec<C,T>::encode(obj, scratch)` then `offer` |
| Vtable hops | 0 | 1 per publish |
| Files | `md/*.h`, `messaging/publishers/md_publisher.h` | `messaging/publishers/{api,aeron}/...` |

## Reading order for new contributors

1. **`src/main.cpp`** — what gets wired up (composition root).
2. **`app/md_gateway_service.{h,cpp}`** — the poll loop. See how `bus_` is consumed.
3. **`messaging/aeron_bus.{h,cpp}`** — what the bus owns (one struct field per stream this service produces/consumes).
4. **`adapter/common/i_adapter.h`** — adapter contract. The file's `@file` doc has an ASCII picture of the per-venue stack.
5. **`adapter/binance/binance_md_adapter.h`** — concrete venue adapter. Other 3 venues follow the same shape.
6. **`adapter/binance/binance_md_decoder.h`** — concept-constrained template doing JSON→domain.
7. **`messaging/publishers/md_publisher.h`** — validate → drop-rate breaker → zero-copy SBE encode + Aeron `tryClaim`. The hot path lives entirely here.

Everything else (subscription manager, individual SBE codecs, per-venue exec
decoders) follows from those seven files.

## Build + test

```bash
bazel build //bpt-md-gateway:bpt-md-gateway
bazel test //bpt-md-gateway/...      # unit + component tests
```

Hot-path latency target: BBO JSON-frame to MD-stream offer in <10 µs p50 on
warm-cache. Measured via `BinanceMdDecoder::decode_lat_` histogram; sampled
to Prometheus every 5 s.

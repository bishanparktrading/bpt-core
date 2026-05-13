# Services

Eight long-running C++ services + one Java MediaDriver, each a `bpt::app::IService`
under systemd. Wire-format contracts via [SBE](https://github.com/aeron-io/simple-binary-encoding); transport via [Aeron IPC](https://github.com/aeron-io/aeron).

| Service | Role | Hot path? |
|---|---|---|
| [bpt-md-gateway](md-gateway.md) | Venue WS → SBE on Aeron | Yes |
| [bpt-order-gateway](order-gateway.md) | Strategy orders → venue REST/WS, exec parse, risk module | Yes |
| [bpt-strategy](strategy.md) | Strategy framework (AS, OFI, momentum, regime-switch, funding-arb) | Yes |
| [bpt-analytics](analytics.md) | Markouts, toxicity scoring, fill-rate, TTF | No |
| [bpt-pricer](pricer.md) | Vol surface fitting, options Greeks | No |
| [bpt-pms](book.md) | Multi-venue balance + position aggregator | No |
| [bpt-refdata](refdata.md) | Instrument catalog, fee schedules | No |
| [bpt-tape](tape.md) | WS-frame recording for backtest replay | No (separate host) |

## Common shape

Every service follows the same outer skeleton:

```cpp
int main(int argc, char* argv[]) {
    auto cfg = config::load(parse_cli(argc, argv));
    install_chaos_from_toml(...);  // optional fault injection (#16)

    return bpt::app::run(service_name, std::move(cfg),
        [](auto& cfg, auto& ctx) -> std::unique_ptr<bpt::app::IService> {
            auto bus = ServiceAeronBus::build(ctx.aeron, cfg);
            return std::make_unique<ServiceApp>(std::move(cfg), std::move(bus));
        });
}
```

The `bpt::app::run` framework handles signal install, logging init,
process-comm naming (`prctl(PR_SET_NAME)`), Aeron client connect, topology pinning,
and the main poll loop. The lambda is the composition root — the only place
that knows about Aeron-backed implementations.

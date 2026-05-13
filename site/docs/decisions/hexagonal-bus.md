# Hexagonal bus boundaries

**Choice:** every Aeron-using service exposes typed port interfaces (`I*Subscriber`,
`I*Publisher`); concrete Aeron bindings live in exactly one `AeronBus::build()`
factory per service.

**Main alternative considered:** direct Aeron coupling in app code (the way most
of the codebase used to be).

## The pattern

```cpp
// Port (in include/<service>/messaging/i_md_publisher.h):
class IMdPublisher {
public:
    virtual ~IMdPublisher() = default;
    virtual void publish(const MdBbo&) = 0;
    virtual void publish(const MdTrade&) = 0;
    virtual void publish(const MdOrderBook&) = 0;
    virtual uint64_t drop_count() const = 0;
};

// Concrete (Aeron-backed):
class MdPublisher final : public IMdPublisher { ... };

// Factory (sole place that #include <Aeron.h>):
struct ServiceBus {
    std::unique_ptr<IMdControlSource> control_source;
    std::shared_ptr<IMdPublisher>     md_sink;
    std::unique_ptr<IAckPublisher>    ack_sink;
    // ...
};

class ServiceAeronBus {
public:
    static ServiceBus build(std::shared_ptr<aeron::Aeron>, const Settings&);
};

// App takes ports, never knows about Aeron:
class ServiceApp {
    ServiceApp(Settings, std::unique_ptr<IMdControlSource>,
               std::shared_ptr<IMdPublisher>, ...);
};
```

The composition root (`main.cpp`) is the only file that knows both `Aeron` AND
`ServiceApp`:

```cpp
auto bus = ServiceAeronBus::build(ctx.aeron, cfg);
return std::make_unique<ServiceApp>(std::move(cfg), std::move(bus));
```

## Why bother

1. **Test seam.** Component tests substitute a `FakeMdPublisher` / `FakeAckPublisher` that capture published frames in a vector — no Aeron in the test build. See `bpt-md-gateway/tests/component/fake_md_publisher.h`.
2. **Single chokepoint for cross-cutting concerns.** [Chaos injection](https://github.com/bishanparktrading/bpt-core/blob/main/bpt-common/include/bpt_common/aeron/chaos_filter.h) hooks into one place per service (the bus factory). Without the seam, every Aeron call site would need to know about the registry.
3. **Forward portability.** If shared-memory IPC ever changes (Chronicle Queue, custom ringbuffer, kernel-bypass like DPDK), only the `*AeronBus::build()` files change. The app, the strategy, the validator — none of them care.

## Why this isn't free

Virtual dispatch on `publish()` is one indirect call per message — measured
overhead is ~2-5 ns vs direct call. Acceptable on the cold path. **Not
acceptable on the publish hot path.**

That's why the hot path uses [CRTP instead](crtp-hot-path.md):
`decoder → ValidatingPublisher<MdPublisher> → MdPublisher` is fully inlined.
The hexagonal pattern lives at the bus seam (cold side); CRTP lives inside the
publish chain (hot side). Both patterns coexist in `bpt-md-gateway`, each in
its right place.

## Where it landed

| Service | Pattern adoption |
|---|---|
| `bpt-refdata` | Full — 5 ports + AeronBus factory (reference impl, [commit](https://github.com/bishanparktrading/bpt-core/commits/main/bpt-refdata)) |
| `bpt-md-gateway` | Full — 4 ports + AeronBus + CRTP on hot path |
| `bpt-order-gateway` | Full — 4 ports + AeronBus |
| `bpt-strategy` | Pragmatic — AeronBus factory, but kept existing client classes concrete (already had `std::function` callbacks; no test-seam payoff yet) |
| `bpt-pricer` / `bpt-analytics` / `bpt-pms` / `bpt-backtester` | AeronBus factory + lifted concretes |

The pragmatic split — formal interfaces where a fake exists or chaos matters,
concrete classes elsewhere — is the right call. Forcing every concrete to a
formal interface adds vtable cost for zero benefit.

## What I'd do differently

The hexagonal sweep landed across all 8 services in a 2-3 day window. In
hindsight, a single bundled refactor was the right call (vs splitting one
service per PR) — the ports themselves stabilised quickly, and bundling kept
reviewer context together.

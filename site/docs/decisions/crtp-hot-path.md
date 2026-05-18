# CRTP on the hot path

**Choice:** the publish chain `decoder → MdPublisher` is templated on the
publisher type, fully vtable-free.

**Main alternative:** virtual `IMdPublisher` interface (which is what we use at
the [bus boundary](hexagonal-bus.md)).

> **Historical note:** earlier revisions interposed a `ValidatingPublisher<MdPublisher>`
> decorator between decoder and publisher. The wrapper was N=1 with its wrapee,
> shared all state, lifetime, and thread affinity, and its `drop_count()` already
> leaked across layers — so it was folded into `MdPublisher` itself. The CRTP
> pattern below is otherwise unchanged.

## The shape

```cpp
class MdPublisher {
public:
    void publish(const MdBbo& bbo)        { forward(bbo); }
    void publish(const MdTrade& trade)    { forward(trade); }
    void publish(const MdOrderBook& book) { forward(book); }

private:
    template <typename T>
    void forward(const T& msg) {
        if (breaker_.tripped()) { validation_drops_++; return; }
        const bool drop = (validator_.validate(msg) != OK);
        breaker_.record(drop, now_ns());
        if (drop) { validation_drops_++; return; }
        // tryClaim + SBE encode in-place; direct call, inlined into decoder.
        publisher_.publish<SbeT>([&](SbeT& m){ /* set fields */ });
    }

    MdValidator validator_;
    ValidationDropBreaker breaker_;
    bpt::common::aeron::Publisher publisher_;
    // ...atomic counters
};
```

The decoder (e.g. `OkxMdDecoder<Pub>`) is templated on `Pub`. So the full
chain at compile time is:

```
OkxMdDecoder<MdPublisher>::decode_bbo(...)
  → MdPublisher::publish(const MdBbo&)
    → validate + breaker + tryClaim + SBE encode
```

After the optimiser has its way, that's a single inlined chain with no
virtual dispatch, no register save/restore for indirect calls, no I-cache
miss on a vtable lookup.

## Why CRTP, not virtual

Virtual dispatch is ~2-5 ns per call. On a hot path that fires hundreds of
thousands of times per second, that's measurable jitter — and worse, it's
*correlated* jitter (an I-cache miss on the vtable affects every subsequent
call until the line evicts again).

CRTP eliminates that. The compiler sees the full chain and inlines aggressively.
The cost is template instantiation pressure (one set of code per `Pub` type) —
acceptable when there are 2-3 distinct `Pub` types per service.

## Where the trade-off bites

`bpt-tape` instantiates `AdapterBase<NoopMdPublisher>` so the publish chain
compiles down to dead branches the optimiser drops. If we'd used virtual
dispatch, `NoopMdPublisher::publish()` would still be a vtable lookup +
indirect call to a function that does nothing. With CRTP, the entire chain
disappears at compile time.

## What this is NOT used for

The bus boundary (the `I*Publisher` ports the app layer holds) stays virtual.
Why: the app layer's calls to publishers are off the hot path (heartbeats,
acks, shutdown notifications). Virtual cost is irrelevant there, and the
test-seam value of port interfaces matters more than 2 ns.

Two patterns. Each in its right place.

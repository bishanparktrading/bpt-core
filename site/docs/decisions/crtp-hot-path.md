# CRTP on the hot path

**Choice:** the publish chain `decoder → ValidatingPublisher<MdPublisher> → MdPublisher`
is templated on the inner publisher type, fully vtable-free.

**Main alternative:** virtual `IMdPublisher` interface (which is what we use at
the [bus boundary](hexagonal-bus.md)).

## The shape

```cpp
template <class Inner>
class ValidatingPublisher {
public:
    void publish(const MdBbo& bbo) { forward(bbo); }
    void publish(const MdTrade& t)  { forward(t); }
    void publish(const MdOrderBook& b) { forward(b); }

private:
    template <typename T>
    void forward(const T& msg) {
        if (validator_.validate(msg) == OK) {
            inner_.publish(msg);  // direct call; inlined into the decoder
        } else {
            drops_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    Inner& inner_;  // not IMdPublisher* — concrete type known at compile time
};
```

The decoder (e.g. `OkxMdDecoder<Pub>`) is also templated on `Pub`. So the
full chain at compile time is:

```
OkxMdDecoder<ValidatingPublisher<MdPublisher>>::decode_bbo(...)
  → ValidatingPublisher<MdPublisher>::publish(MdBbo&)
    → MdPublisher::publish(const MdBbo&)
      → tryClaim() + SBE encode
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

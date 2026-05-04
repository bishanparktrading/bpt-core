# bpt-pricer

Vol surface fitting + options Greeks. Consumes options MD + index ticks; emits
a fitted vol surface for downstream consumers (planned: an options-MM strategy).

## Status

Foundational — vol surface client + Greeks computations exist; the consuming
strategy is in design. The pricer currently runs against Deribit (the only
options venue in the universe) and OKX (perp index for forward calculation).

## Architecture

Smallest of the bus-boundary refactors — `PricerAeronBus::build()` returns
4 concretes (no formal port interfaces yet). The hot path on this service is
modest (one fit per instrument per N seconds) so the CRTP optimisation pattern
from `bpt-md-gateway` doesn't apply here.

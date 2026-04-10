<p align="center">
  <img src="assets/banner.png" alt="yggdrasil" width="200"/>
</p>

# yggdrasil

Shared header-only C++ utility library for the Fenrir trading system.

All C++ services (`sindri`, `fenrir`, `huginn`, `heimdall`, `surtr`, `jormungandr`) link against this as a CMake `INTERFACE` target — no compilation required.

## Requirements

- C++20
- spdlog (logging, signal, thread_pin, tsc_clock)
- Aeron C++ client (aeron_utils)
- Boost.Beast + Boost.Asio + OpenSSL (ws_connect)
- fast_float + simdjson (parse_double)

## CMake integration

```cmake
add_subdirectory(path/to/yggdrasil)
target_link_libraries(my_service PRIVATE yggdrasil)
```

## Headers

**`aeron/`**

| Header | Namespace | Description |
|---|---|---|
| `aeron/aeron_utils.h` | `ygg::aeron` | `wait_for_publication`, `wait_for_subscription`, `connect` — spin until Aeron connects |
| `aeron/stream_config.h` | `ygg::config` | `StreamConfig{channel, stream_id}` — Aeron addressing unit, no external deps |

**`util/`**

| Header | Namespace | Description |
|---|---|---|
| `util/latency_histogram.h` | `ygg::util` | Lock-free power-of-2 bucket histogram; ~5ns `record()` cost; p50/p99/max/mean via `Snapshot` |
| `util/parse_double.h` | `ygg::util` | `ff_double` — fast_float wrapper for quoted JSON number fields (drop-in for simdjson `get_double_in_string`) |
| `util/spsc_queue.h` | `ygg::util` | `SpscQueue<CAPACITY, MAX_PAYLOAD_BYTES>` — lock-free SPSC ring buffer for IO→parser handoff |
| `util/thread_pin.h` | `ygg::util` | `pin_thread_to_cpu(cpu_id, name)` — Linux `pthread_setaffinity_np` wrapper |
| `util/tsc_clock.h` | `ygg::util` | `TscClock` — invariant-TSC wall clock and monotonic clock; ~4ns vs ~20ns for vDSO |

**`ws/`**

| Header | Namespace | Description |
|---|---|---|
| `ws/ws_connect.h` | `ygg::ws` | `ws_connect(...)` — TLS WebSocket connect (Boost.Beast): DNS + TCP + TLS + WS upgrade |

**Root**

| Header | Namespace | Description |
|---|---|---|
| `logging.h` | `ygg::logging` | Async spdlog init — rotating file + colour console; `level_from_string` |
| `signal.h` | `ygg::signal` | `install()` / `is_running()` / `stop()` — unified SIGINT/SIGTERM handling |

## Usage examples

```cpp
#include <yggdrasil/signal.h>
#include <yggdrasil/logging.h>
#include <yggdrasil/aeron/aeron_utils.h>
#include <yggdrasil/aeron/stream_config.h>
#include <yggdrasil/util/tsc_clock.h>
#include <yggdrasil/util/thread_pin.h>
#include <yggdrasil/util/spsc_queue.h>
#include <yggdrasil/util/latency_histogram.h>
#include <yggdrasil/ws/ws_connect.h>

// Signal handling
ygg::signal::install();
while (ygg::signal::is_running()) { /* main loop */ }

// Logging
ygg::logging::init("fenrir", "logs", spdlog::level::info);

// TSC clock — call once at startup
ygg::util::TscClock::calibrate();
uint64_t t = ygg::util::TscClock::now_epoch_ns();

// Thread pinning
ygg::util::pin_thread_to_cpu(3, "md_thread");

// Aeron
auto aeron = ygg::aeron::connect("/dev/shm/aeron-bifrost");
auto pub   = ygg::aeron::wait_for_publication(aeron, "aeron:ipc", 2001);
auto sub   = ygg::aeron::wait_for_subscription(aeron, "aeron:ipc", 2002);

// SPSC queue (IO thread → parser thread)
ygg::util::SpscQueue<512, 16384> q;
q.try_push(recv_ns, payload);
q.try_pop([](uint64_t recv_ns, std::string_view data) { /* parse */ });

// Latency histogram
ygg::util::LatencyHistogram hist;
hist.record(end_ns - start_ns);
auto snap = hist.snapshot_and_reset();
spdlog::info("p50={}ns p99={}ns max={}ns", snap.percentile_ns(0.50),
             snap.percentile_ns(0.99), snap.max_ns());

// WebSocket connect
boost::asio::io_context ioc;
boost::asio::ssl::context ssl_ctx{boost::asio::ssl::context::tls_client};
ssl_ctx.set_default_verify_paths();
auto ws = ygg::ws::ws_connect(ioc, ssl_ctx, "stream.binance.com", "9443", "/ws");
```

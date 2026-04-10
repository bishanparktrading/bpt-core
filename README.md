# bpt-core

Monorepo for the Fenrir low-latency algorithmic trading system. All inter-service communication runs over [Aeron](https://github.com/real-logic/aeron) via the Bifrost-fabric media driver.

## Services

| Service | Language | Role |
|---|---|---|
| **fenrir** | C++ | Trading engine — strategy logic, order management |
| **huginn** | C++ | Market data gateway (Binance, OKX, Hyperliquid, Deribit) |
| **heimdall** | C++ | Order gateway — routes orders to exchanges, returns execution reports |
| **muninn** | C++ | Reference data service — instruments, fee schedules |
| **surtr** | C++ | Implied volatility surface computation |
| **jormungandr** | C++ | Backtester — exchange simulator, reads Parquet data from S3 |
| **bifrost/fabric** | Java | Aeron media driver — central messaging backbone |
| **bifrost/protocol** | C++ | SBE message schemas and generated codecs |

## Requirements

### System

- GCC 13+ (C++23)
- CMake 3.20+
- Ninja
- OpenSSL 3
- Arrow & Parquet (Apache apt repo — see CI workflow for install steps)
- Java 17 (for bifrost-fabric)

### vcpkg packages

Installed automatically during CMake configure:

```
fmt  spdlog  tomlplusplus  boost-beast  boost-asio  boost-json  boost-system
simdjson  openssl  nlohmann-json  gtest  prometheus-cpp  aws-sdk-cpp[s3,secretsmanager]
```

### FetchContent (auto-downloaded during configure)

- [Aeron](https://github.com/real-logic/aeron) 1.44.1
- [fast_float](https://github.com/fastfloat/fast_float) v6.1.6

## Building

```bash
# Clone vcpkg
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh -disableMetrics

# Configure (Debug)
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake

# Build everything
cmake --build build -j$(nproc)

# Run tests
ctest --test-dir build --output-on-failure -j$(nproc)
```

## Running the stack (OKX demo)

```bash
# Start all services with testnet/demo configs
./scripts/stack-testnet.sh start

# Check status
./scripts/stack-testnet.sh status

# Stop
./scripts/stack-testnet.sh stop
```

**Startup order:** bifrost-fabric → muninn → huginn + heimdall (parallel) → fenrir

Fenrir blocks at startup until muninn publishes `RefDataReady` on stream 1006. If any configured exchange is missing from that signal, fenrir halts.

## Deploying to a remote host

Build a release tarball:

```bash
./scripts/package.sh --version 1.0.0 --out-dir dist/
# → dist/bpt-core-1.0.0-linux-x86_64.tar.gz
```

Copy and install:

```bash
scp dist/bpt-core-1.0.0-linux-x86_64.tar.gz user@host:/opt/bpt/
ssh user@host 'cd /opt/bpt && tar -xzf bpt-core-1.0.0-linux-x86_64.tar.gz \
  && cd bpt-core-1.0.0-linux-x86_64 && sudo ./install.sh'
```

Tagged releases are also published automatically as GitHub Release assets — push a `v*` tag to trigger the release workflow.

## Project layout

```
bpt-core/
  fenrir/
    include/fenrir/   # public headers
    src/              # implementation + main.cpp
    config/           # per-environment TOML configs
    tests/
  huginn/             # same layout
  heimdall/
  muninn/
  surtr/
  jormungandr/
  bifrost/
    fabric/           # Java media driver (Gradle)
    protocol/         # SBE schemas + generated C++ codecs
  third_party/
    yggdrasil/        # shared C++ utility library (logging, Aeron utils, WS, ...)
  scripts/
    stack.sh          # start/stop/status full stack
    stack-testnet.sh  # start/stop/status with OKX demo configs
    package.sh        # build release tarball
  .github/workflows/
    ci.yml            # build + test on push/PR
    release.yml       # package + publish on v* tags
```

## Aeron stream assignments

| Stream | Direction | Messages |
|---|---|---|
| 1001 | muninn → fenrir | RefDataSnapshot |
| 1002 | muninn → fenrir | RefDataDelta, Heartbeat |
| 1003 | fenrir → muninn | RefDataSubscriptionRequest |
| 1004 | muninn → fenrir | FeeSchedule |
| 1005 | huginn → fenrir | FundingRate |
| 1006 | muninn → fenrir | RefDataReady, RefDataError |
| 2001 | fenrir → huginn | MdSubscribeBatch |
| 2002 | huginn → fenrir | MdMarketData, MdTrade, MdOrderBook |
| 2003 | huginn → fenrir | MdSubscriptionAck, Heartbeats |
| 3001 | fenrir → heimdall | NewOrder, CancelOrder, ModifyOrder, CancelAll |
| 3002 | heimdall → fenrir | ExecutionReport |
| 3003 | heimdall → fenrir | HeimdallHeartbeat |
| 4001 | surtr → fenrir | VolSurface |
| 4002 | surtr → fenrir | SurtrHeartbeat, SurtrReady |
| 9001 | fenrir → jormungandr | BacktestAck (backtest mode only) |
| 9002 | jormungandr → fenrir | BacktestControl (backtest mode only) |

## Configuration

Each service has a TOML config. The `[logging]` section is common to all:

```toml
[logging]
level             = "info"    # trace/debug/info/warn/error/critical/off
dir               = "logs"
flush_level       = "warn"    # force-flush on messages at this level or above
flush_interval_ms = 0         # also flush every N ms (0 = disable)
console           = true
file              = true
max_file_size_mb  = 10
max_files         = 3
```

Exchange credentials are loaded from AWS Secrets Manager at runtime. Set `BPT_ENV=local` to load from `~/.bpt-secrets/` instead (see each service's config for the expected secret names).

## CI

Two GitHub Actions workflows:

- **ci.yml** — runs on every push and PR to `main`; builds Debug and runs all tests
- **release.yml** — runs on `v*` tags; builds Release and publishes a deployment tarball as a GitHub Release asset

Caches: vcpkg installation, compiled vcpkg packages (keyed on `vcpkg.json`), and FetchContent downloads (keyed on `CMakeLists.txt`).

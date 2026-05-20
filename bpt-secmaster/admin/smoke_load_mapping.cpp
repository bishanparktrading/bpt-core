// Smoke test: loads an instrument_mapping.json file via the actual
// bpt-refdata InstrumentMappingLoader and prints summary stats.
//
// Build:  bazel build //bpt-secmaster/admin:smoke_load_mapping
// Run:    bazel-bin/bpt-secmaster/admin/smoke_load_mapping /tmp/secmaster-rendered-mapping.json
//
// Exists to validate that secmaster-rendered files can be consumed by
// the existing bpt-refdata loader unchanged. If this binary loads the
// file without throwing AND the counts look right, the Path A sidecar
// is wire-compatible with the existing C++ trading stack.

#include "refdata/mapping/instrument_mapping_loader.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

using bpt::refdata::mapping::EXCHANGE_ID_BINANCE;
using bpt::refdata::mapping::EXCHANGE_ID_DERIBIT;
using bpt::refdata::mapping::EXCHANGE_ID_HYPERLIQUID;
using bpt::refdata::mapping::EXCHANGE_ID_OKX;
using bpt::refdata::mapping::InstrumentMappingLoader;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <path/to/instrument_mapping.json>\n", argv[0]);
        return 2;
    }
    const char* path = argv[1];

    InstrumentMappingLoader loader;
    try {
        loader.load(path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "LOAD FAILED: %s\n", e.what());
        return 1;
    }

    std::printf("✓ loaded %zu instruments from %s\n", loader.instrument_count(), path);

    // Per-venue listing counts via the public catalog view.
    struct VenueInfo {
        uint8_t id;
        const char* name;
    };
    const VenueInfo venues[] = {
        {EXCHANGE_ID_BINANCE, "binance"},
        {EXCHANGE_ID_OKX, "okx"},
        {EXCHANGE_ID_HYPERLIQUID, "hl"},
        {EXCHANGE_ID_DERIBIT, "deribit"},
    };
    for (const auto& v : venues) {
        auto rows = loader.instruments_for_venue(v.id);
        std::printf("  %-10s %zu listings\n", v.name, rows.size());
    }

    // Spot-check a few resolves we expect to find (specific instruments
    // may not exist depending on what the refresher pulled, so use
    // try_resolve_* which doesn't log on miss).
    struct Probe {
        uint8_t exchange;
        const char* symbol;
        const char* label;
    };
    const Probe probes[] = {
        {EXCHANGE_ID_BINANCE, "BTCUSDT", "Binance PERP BTCUSDT"},
        {EXCHANGE_ID_OKX, "BTC-USDT-SWAP", "OKX PERP BTC-USDT-SWAP"},
        {EXCHANGE_ID_HYPERLIQUID, "BTC", "HL PERP BTC"},
    };
    std::printf("\nSpot-check resolves:\n");
    for (const auto& p : probes) {
        auto id = loader.try_resolve_canonical_id(p.exchange, p.symbol);
        if (id) {
            std::printf("  %-30s -> canonical_id=%u\n", p.label, *id);
        } else {
            std::printf("  %-30s -> (not found)\n", p.label);
        }
    }

    return 0;
}

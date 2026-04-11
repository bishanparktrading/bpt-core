#pragma once

// yggdrasil/tsc_clock.h — Fast timestamps using the CPU invariant TSC.
//
// Requires: x86-64 with invariant TSC (all modern Intel/AMD CPUs since ~2008).
//
// Usage:
//   ygg::util::TscClock::calibrate();              // once at startup, blocks ~10ms
//   uint64_t t = ygg::util::TscClock::now_epoch_ns();  // ~4ns vs ~20ns for vDSO

#include <cstdint>
#include <x86intrin.h>

namespace ygg::util {

// Fast monotonic and wall-clock timestamps using the CPU invariant TSC.
//
// Call TscClock::calibrate() once at startup before any reads.
// All now_*() methods are lock-free and branch-free; typical cost ~4ns vs
// ~20ns for clock_gettime(CLOCK_MONOTONIC) even with vDSO.
//
// now_epoch_ns() accuracy: ±1µs at calibration, drifts <1µs/hour on modern
// CPUs with a stable invariant TSC.  Re-calibrate if the process runs >24h.
class TscClock {
public:
    // Calibrate TSC frequency and wall-clock anchor.  Blocks for ~10ms.
    // Must be called once before now_epoch_ns() or now_mono_ns().
    static void calibrate() noexcept;

    // Nanoseconds since Unix epoch — fast drop-in for system_clock::now().
    [[nodiscard]] static inline uint64_t now_epoch_ns() noexcept {
        uint64_t delta = __rdtsc() - ref_tsc_;
        return wall_anchor_ns_ + static_cast<uint64_t>(static_cast<double>(delta) * ns_per_tsc_);
    }

    // Monotonic nanoseconds — fast drop-in for clock_gettime(CLOCK_MONOTONIC).
    // Use for latency measurement (deltas only; epoch is arbitrary).
    [[nodiscard]] static inline uint64_t now_mono_ns() noexcept {
        return static_cast<uint64_t>(static_cast<double>(__rdtsc()) * ns_per_tsc_);
    }

    [[nodiscard]] static double tsc_ghz() noexcept { return 1.0 / ns_per_tsc_; }

private:
    // C++17 inline statics — defined once here, no .cpp required.
    inline static double ns_per_tsc_ = 1.0;
    inline static uint64_t ref_tsc_ = 0;
    inline static uint64_t wall_anchor_ns_ = 0;
};

}  // namespace ygg::util

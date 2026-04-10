#pragma once

// yggdrasil/tsc_clock.h — Fast timestamps using the CPU invariant TSC.
//
// Requires: x86-64 with invariant TSC (all modern Intel/AMD CPUs since ~2008).
// Dependencies: spdlog (for calibrate() log output).
//
// Usage:
//   ygg::util::TscClock::calibrate();       // once at startup, blocks ~10ms
//   uint64_t t = ygg::util::TscClock::now_epoch_ns();  // ~4ns vs ~20ns for vDSO

#include <cstdint>
#include <spdlog/spdlog.h>
#include <time.h>
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
    static void calibrate() noexcept {
        // Warm up the TSC and clock_gettime paths.
        for (int i = 0; i < 8; ++i) {
            (void)__rdtsc();
            (void)read_monotonic_raw_ns();
        }

        // Spin-wait for a CLOCK_MONOTONIC_RAW tick boundary to minimise the
        // gap between the kernel timestamp and the TSC read.
        uint64_t mono0 = read_monotonic_raw_ns();
        uint64_t tsc0;
        {
            uint64_t m;
            do {
                m = read_monotonic_raw_ns();
            } while (m == mono0);
            mono0 = m;
            tsc0 = __rdtsc();
        }

        // Sleep ~10ms to accumulate enough TSC ticks for accurate rate measurement.
        struct timespec req = {0, 10'000'000};
        nanosleep(&req, nullptr);

        // Take end sample at a tick boundary.
        uint64_t mono1 = read_monotonic_raw_ns();
        uint64_t tsc1;
        {
            uint64_t m;
            do {
                m = read_monotonic_raw_ns();
            } while (m == mono1);
            mono1 = m;
            tsc1 = __rdtsc();
        }

        ns_per_tsc_ = static_cast<double>(mono1 - mono0) / static_cast<double>(tsc1 - tsc0);

        // Anchor the wall clock to the current TSC.  Repeat 16 times and take
        // the tightest CLOCK_REALTIME pair to minimise jitter.
        uint64_t best_wall = 0, best_tsc = 0, best_gap = UINT64_MAX;
        for (int i = 0; i < 16; ++i) {
            uint64_t w0 = read_realtime_ns();
            uint64_t tsc = __rdtsc();
            uint64_t w1 = read_realtime_ns();
            uint64_t gap = w1 - w0;
            if (gap < best_gap) {
                best_gap = gap;
                best_wall = w0 + gap / 2;
                best_tsc = tsc;
            }
        }

        ref_tsc_ = best_tsc;
        wall_anchor_ns_ = best_wall;

        spdlog::info("TscClock calibrated: {:.4f} GHz, wall anchor err ~{}ns", tsc_ghz(), best_gap / 2);
    }

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
    static inline uint64_t read_monotonic_raw_ns() noexcept {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
    }

    static inline uint64_t read_realtime_ns() noexcept {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
    }

    // C++17 inline static — defined once, no .cpp required.
    inline static double ns_per_tsc_ = 1.0;
    inline static uint64_t ref_tsc_ = 0;
    inline static uint64_t wall_anchor_ns_ = 0;
};

}  // namespace ygg::util

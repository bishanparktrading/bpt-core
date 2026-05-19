#include "backtester/latency/latency_model.h"

#include <gtest/gtest.h>
#include <unordered_set>

using bpt::backtester::latency::LatencyLeg;
using bpt::backtester::latency::ParametricLatencyModel;

TEST(ParametricLatencyModel, ZeroSpecReturnsZero) {
    ParametricLatencyModel m(/*seed=*/1);
    EXPECT_EQ(m.draw("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH), 0u);
    EXPECT_EQ(m.draw("HYPERLIQUID", LatencyLeg::MATCH_TO_REPORT), 0u);
}

TEST(ParametricLatencyModel, BaseOnlyIsConstant) {
    ParametricLatencyModel m(/*seed=*/42);
    m.set_spec("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH, {200'000'000ULL, 0});
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(m.draw("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH), 200'000'000ULL);
}

TEST(ParametricLatencyModel, JitterStaysWithinHalfOpenInterval) {
    ParametricLatencyModel m(/*seed=*/123);
    m.set_spec("OKX", LatencyLeg::SUBMIT_TO_MATCH, {/*base=*/1'000'000ULL, /*jitter=*/500'000ULL});
    uint64_t lo = std::numeric_limits<uint64_t>::max();
    uint64_t hi = 0;
    for (int i = 0; i < 10'000; ++i) {
        const uint64_t v = m.draw("OKX", LatencyLeg::SUBMIT_TO_MATCH);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    EXPECT_GE(lo, 1'000'000ULL);
    EXPECT_LT(hi, 1'500'000ULL);  // [base, base+jitter)
}

TEST(ParametricLatencyModel, SameSeedSameSequence) {
    ParametricLatencyModel a(/*seed=*/7);
    ParametricLatencyModel b(/*seed=*/7);
    a.set_spec("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH, {0, 1'000'000});
    b.set_spec("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH, {0, 1'000'000});
    for (int i = 0; i < 1000; ++i)
        EXPECT_EQ(a.draw("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH),
                  b.draw("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH));
}

TEST(ParametricLatencyModel, DifferentSeedsDifferentSequences) {
    ParametricLatencyModel a(/*seed=*/1);
    ParametricLatencyModel b(/*seed=*/2);
    a.set_spec("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH, {0, 1'000'000});
    b.set_spec("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH, {0, 1'000'000});
    int diff = 0;
    for (int i = 0; i < 100; ++i) {
        if (a.draw("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH) != b.draw("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH))
            ++diff;
    }
    EXPECT_GT(diff, 90);  // overwhelmingly likely with mt19937 + different seeds
}

TEST(ParametricLatencyModel, PerVenueSpecsAreIndependent) {
    ParametricLatencyModel m(/*seed=*/1);
    m.set_spec("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH, {200'000'000ULL, 0});
    m.set_spec("OKX", LatencyLeg::SUBMIT_TO_MATCH, {2'000'000ULL, 0});
    EXPECT_EQ(m.draw("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH), 200'000'000ULL);
    EXPECT_EQ(m.draw("OKX", LatencyLeg::SUBMIT_TO_MATCH), 2'000'000ULL);
}

TEST(ParametricLatencyModel, UnknownVenueFallsBackToDefault) {
    ParametricLatencyModel m(/*seed=*/1);
    m.set_default(LatencyLeg::SUBMIT_TO_MATCH, {5'000'000ULL, 0});
    EXPECT_EQ(m.draw("BITMEX_OR_WHATEVER", LatencyLeg::SUBMIT_TO_MATCH), 5'000'000ULL);
}

TEST(ParametricLatencyModel, LegSpecsAreIndependent) {
    ParametricLatencyModel m(/*seed=*/1);
    m.set_spec("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH, {200'000'000ULL, 0});
    m.set_spec("HYPERLIQUID", LatencyLeg::MATCH_TO_REPORT, {50'000'000ULL, 0});
    EXPECT_EQ(m.draw("HYPERLIQUID", LatencyLeg::SUBMIT_TO_MATCH), 200'000'000ULL);
    EXPECT_EQ(m.draw("HYPERLIQUID", LatencyLeg::MATCH_TO_REPORT), 50'000'000ULL);
}

#include "features/ewma.h"

#include <cmath>
#include <gtest/gtest.h>

namespace {

using bpt::features::EwmaDrift;
using bpt::features::EwmaVariance;
using bpt::features::KappaEstimator;
using bpt::features::TimeWeightedEwma;

constexpr uint64_t kSecond = 1'000'000'000ULL;

TEST(TimeWeightedEwmaTest, ConvergesToConstantObs) {
    TimeWeightedEwma e(/*halflife_s=*/1.0);
    for (int i = 0; i < 100; ++i)
        e.update(5.0, 0.1);
    EXPECT_NEAR(e.value(), 5.0, 0.01);
    EXPECT_EQ(e.count(), 100u);
}

TEST(TimeWeightedEwmaTest, RejectsZeroDt) {
    TimeWeightedEwma e(1.0);
    e.update(5.0, 0.0);
    EXPECT_EQ(e.value(), 0.0);
    EXPECT_EQ(e.count(), 0u);
}

TEST(TimeWeightedEwmaTest, ResetClears) {
    TimeWeightedEwma e(1.0);
    e.update(5.0, 0.1);
    e.update(5.0, 0.1);
    EXPECT_GT(e.value(), 0.0);
    e.reset();
    EXPECT_EQ(e.value(), 0.0);
    EXPECT_EQ(e.count(), 0u);
}

TEST(EwmaVarianceTest, FirstUpdateNoOp) {
    EwmaVariance v(1.0);
    v.update(100.0, kSecond);
    // No prior mid — first update only sets baseline.
    EXPECT_EQ(v.value(), 0.0);
    EXPECT_EQ(v.count(), 0u);
}

TEST(EwmaVarianceTest, ConstantPriceZeroVariance) {
    EwmaVariance v(1.0);
    for (int i = 1; i <= 50; ++i)
        v.update(100.0, i * kSecond);
    EXPECT_NEAR(v.value(), 0.0, 1e-12);
    EXPECT_EQ(v.count(), 49u);
}

TEST(EwmaVarianceTest, AccumulatesUnderRandomWalk) {
    EwmaVariance v(1.0);
    double mid = 100.0;
    for (int i = 1; i <= 100; ++i) {
        mid *= (i % 2 == 0) ? 1.001 : 0.999;  // alternating ±0.1%
        v.update(mid, i * kSecond);
    }
    EXPECT_GT(v.value(), 0.0);
    EXPECT_EQ(v.count(), 99u);  // first update sets baseline only
}

TEST(EwmaDriftTest, PositiveDriftOnRisingPrice) {
    EwmaDrift d(1.0);
    for (int i = 1; i <= 50; ++i)
        d.update(100.0 * std::exp(0.0001 * i), i * kSecond);  // steady uptrend
    EXPECT_GT(d.value(), 0.0);
}

TEST(EwmaDriftTest, NegativeDriftOnFallingPrice) {
    EwmaDrift d(1.0);
    for (int i = 1; i <= 50; ++i)
        d.update(100.0 * std::exp(-0.0001 * i), i * kSecond);
    EXPECT_LT(d.value(), 0.0);
}

TEST(KappaEstimatorTest, FirstUpdateNoOp) {
    KappaEstimator k(1.0);
    k.update(kSecond);
    EXPECT_EQ(k.value(), 0.0);
    EXPECT_EQ(k.count(), 0u);
}

TEST(KappaEstimatorTest, ConvergesToHalfArrivalRate) {
    KappaEstimator k(/*halflife_s=*/1.0);
    // 1 trade per 100ms → 10 trades/s total; per-side kappa = 5.0
    for (int i = 1; i <= 200; ++i)
        k.update(i * kSecond / 10);
    EXPECT_NEAR(k.value(), 5.0, 0.1);
}

}  // namespace

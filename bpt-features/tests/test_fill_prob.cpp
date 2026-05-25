#include "features/fill_prob.h"

#include <cmath>
#include <gtest/gtest.h>

namespace {

using bpt::features::fill_probability_geometric;
using bpt::features::fill_probability_poisson;

// ── Poisson form ────────────────────────────────────────────────────────────

TEST(FillProbabilityPoissonTest, FrontOfQueueReturnsOne) {
    EXPECT_DOUBLE_EQ(fill_probability_poisson(/*kappa=*/10.0, /*T=*/5.0, /*queue_ahead=*/0.0), 1.0);
    EXPECT_DOUBLE_EQ(fill_probability_poisson(10.0, 5.0, -1.0), 1.0);
}

TEST(FillProbabilityPoissonTest, DeadMarketReturnsZero) {
    EXPECT_DOUBLE_EQ(fill_probability_poisson(/*kappa=*/0.0, 5.0, 100.0), 0.0);
    EXPECT_DOUBLE_EQ(fill_probability_poisson(-1.0, 5.0, 100.0), 0.0);
}

TEST(FillProbabilityPoissonTest, ZeroHorizonReturnsZero) {
    EXPECT_DOUBLE_EQ(fill_probability_poisson(10.0, 0.0, 100.0), 0.0);
}

TEST(FillProbabilityPoissonTest, KappaTEqualsQueueAheadGivesOneMinus1OverE) {
    // When kappa*T == queue_ahead, the expected count of arrivals over T
    // exactly equals the queue we need to clear → P = 1 - 1/e ≈ 0.632.
    const double p = fill_probability_poisson(/*kappa=*/2.0, /*T=*/5.0, /*queue_ahead=*/10.0);
    EXPECT_NEAR(p, 1.0 - 1.0 / std::exp(1.0), 1e-12);
}

TEST(FillProbabilityPoissonTest, MonotoneInKappa) {
    const double p1 = fill_probability_poisson(1.0, 5.0, 100.0);
    const double p2 = fill_probability_poisson(10.0, 5.0, 100.0);
    EXPECT_LT(p1, p2);
}

TEST(FillProbabilityPoissonTest, MonotoneInHorizon) {
    const double p1 = fill_probability_poisson(2.0, 1.0, 100.0);
    const double p2 = fill_probability_poisson(2.0, 10.0, 100.0);
    EXPECT_LT(p1, p2);
}

TEST(FillProbabilityPoissonTest, MonotoneInQueueAheadInverse) {
    const double p_near = fill_probability_poisson(2.0, 5.0, 10.0);
    const double p_far = fill_probability_poisson(2.0, 5.0, 1000.0);
    EXPECT_GT(p_near, p_far);
}

TEST(FillProbabilityPoissonTest, LargeHorizonAsymptotesToOne) {
    EXPECT_NEAR(fill_probability_poisson(10.0, 1000.0, 1.0), 1.0, 1e-10);
}

// ── Geometric form ──────────────────────────────────────────────────────────

TEST(FillProbabilityGeometricTest, FrontOfQueueReturnsOne) {
    EXPECT_DOUBLE_EQ(fill_probability_geometric(/*our_qty=*/10.0, /*queue_ahead=*/0.0), 1.0);
    EXPECT_DOUBLE_EQ(fill_probability_geometric(10.0, -5.0), 1.0);
}

TEST(FillProbabilityGeometricTest, ZeroOurQtyReturnsZero) {
    EXPECT_DOUBLE_EQ(fill_probability_geometric(0.0, 100.0), 0.0);
}

TEST(FillProbabilityGeometricTest, EqualSizeGivesHalf) {
    EXPECT_DOUBLE_EQ(fill_probability_geometric(50.0, 50.0), 0.5);
}

TEST(FillProbabilityGeometricTest, ShareScalesCorrectly) {
    EXPECT_NEAR(fill_probability_geometric(1.0, 9.0), 0.1, 1e-12);
    EXPECT_NEAR(fill_probability_geometric(1.0, 99.0), 0.01, 1e-12);
}

}  // namespace

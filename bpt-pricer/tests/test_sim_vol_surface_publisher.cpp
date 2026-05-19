/// \file
/// \brief Tests for sim::VolSurfacePublisher — the codec-bypass concrete
/// of api::VolSurfacePublisher used by the deterministic backtester.
///
/// The whole purpose of this concrete is to dispatch domain
/// VolSurfaceGrid directly without going through SBE encode + Aeron
/// offer. The tests assert: (1) every publish() reaches the handler
/// verbatim — same grid identity, no field loss; (2) null handler is
/// safe to publish into (no-op, no crash); (3) the publisher
/// satisfies the api::VolSurfacePublisher port contract callers depend on.

#include "pricer/messaging/publishers/sim/vol_surface_publisher.h"

#include <messages/OptionSide.h>

#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace {

namespace api = bpt::pricer::messaging::api;
namespace sim = bpt::pricer::messaging::sim;
using bpt::pricer::surface::IvPoint;
using bpt::pricer::surface::VolSurfaceGrid;

VolSurfaceGrid make_grid(uint64_t seq, std::size_t n_points) {
    VolSurfaceGrid g;
    g.exchange_id = bpt::messages::ExchangeId::DERIBIT;
    g.underlying = "ETH";
    g.seq_num = seq;
    g.points.reserve(n_points);
    for (std::size_t i = 0; i < n_points; ++i) {
        IvPoint p{};
        p.instrument_id = 1000 + i;
        p.expiry_date = 20260516;
        p.strike_price = 2000.0 + 100.0 * i;
        p.option_side = bpt::messages::OptionSide::CALL;
        p.implied_vol = 0.7;
        g.points.push_back(p);
    }
    return g;
}

}  // namespace

TEST(SimVolSurfacePublisher, DispatchesGridVerbatimToHandler) {
    struct Capture {
        VolSurfaceGrid grid;
        uint64_t ts = 0;
        int count = 0;
    };
    Capture cap;
    sim::VolSurfacePublisher pub([&cap](const VolSurfaceGrid& g, uint64_t ts) {
        cap.grid = g;
        cap.ts = ts;
        ++cap.count;
    });

    const auto in = make_grid(7, 3);
    pub.publish(in, /*ts=*/1'700'000'000'000'000'000ULL);

    EXPECT_EQ(cap.count, 1);
    EXPECT_EQ(cap.ts, 1'700'000'000'000'000'000ULL);
    EXPECT_EQ(cap.grid.seq_num, 7u);
    EXPECT_EQ(cap.grid.underlying, "ETH");
    ASSERT_EQ(cap.grid.points.size(), 3u);
    EXPECT_EQ(cap.grid.points[0].instrument_id, 1000u);
    EXPECT_DOUBLE_EQ(cap.grid.points[1].strike_price, 2100.0);
}

TEST(SimVolSurfacePublisher, MultiplePublishesAccumulate) {
    int count = 0;
    sim::VolSurfacePublisher pub([&count](const VolSurfaceGrid&, uint64_t) { ++count; });

    for (int i = 0; i < 5; ++i)
        pub.publish(make_grid(i, 2), static_cast<uint64_t>(i));

    EXPECT_EQ(count, 5);
}

TEST(SimVolSurfacePublisher, NullHandlerIsNoOp) {
    sim::VolSurfacePublisher pub(nullptr);
    // Should not throw, should not crash.
    pub.publish(make_grid(1, 1), 1234);
    SUCCEED();
}

TEST(SimVolSurfacePublisher, SatisfiesPortContract) {
    // The concrete must be substitutable for the port — verified by
    // upcasting to the port interface and calling through it.
    bool called = false;
    std::unique_ptr<api::VolSurfacePublisher> port =
        std::make_unique<sim::VolSurfacePublisher>([&called](const VolSurfaceGrid&, uint64_t) { called = true; });

    port->publish(make_grid(0, 0), 0);
    EXPECT_TRUE(called);
}

/// \file
/// \brief Round-trip + edge-case tests for SbeVolSurfaceCodec.
///
/// First test file in the bpt-pricer tree that exercises a *codec* in
/// isolation. Previously the SBE encode lived inside
/// aeron::VolSurfacePublisher::publish — testing it required standing up
/// an Aeron pub/sub pair. With the codec extracted as a pure utility,
/// these tests run in microseconds without any IPC.

#include "pricer/messaging/codecs/sbe_vol_surface_codec.h"

#include <messages/OptionSide.h>

#include <array>
#include <cstddef>
#include <gtest/gtest.h>
#include <stdexcept>

namespace {

using bpt::pricer::messaging::SbeVolSurfaceCodec;
using bpt::pricer::surface::IvPoint;
using bpt::pricer::surface::VolSurfaceGrid;

IvPoint make_point(uint64_t inst_id, double strike, bool is_call) {
    IvPoint p;
    p.instrument_id = inst_id;
    p.expiry_date = 20260516;
    p.strike_price = strike;
    p.option_side = is_call ? bpt::messages::OptionSide::CALL : bpt::messages::OptionSide::PUT;
    p.implied_vol = 0.65;
    p.forward_price = 30000.5;
    p.time_to_expiry = 0.0833;  // ~30 days
    p.bid_iv = 0.64;
    p.ask_iv = 0.66;
    p.bid_price = 750.0;
    p.ask_price = 760.0;
    p.delta = is_call ? 0.5 : -0.5;
    p.gamma = 0.0012;
    p.vega = 12.3;
    p.theta = -45.6;
    return p;
}

VolSurfaceGrid make_grid_with(std::size_t n_points) {
    VolSurfaceGrid g;
    g.exchange_id = bpt::messages::ExchangeId::DERIBIT;
    g.underlying = "BTC";
    g.seq_num = 42;
    g.points.reserve(n_points);
    for (std::size_t i = 0; i < n_points; ++i)
        g.points.push_back(make_point(/*inst=*/100 + i, /*strike=*/28000.0 + 500.0 * i, /*is_call=*/(i % 2) == 0));
    return g;
}

constexpr uint64_t kTs = 1'700'000'000'000'000'000ULL;

}  // namespace

TEST(SbeVolSurfaceCodec, RoundTripPreservesAllFields) {
    SbeVolSurfaceCodec codec;
    const auto in = make_grid_with(5);

    alignas(8) std::array<std::byte, SbeVolSurfaceCodec::kRecommendedScratchSize> scratch{};
    const auto bytes = codec.encode(in, kTs, scratch);
    ASSERT_GT(bytes.size(), 0u);

    const auto out = codec.decode(bytes);

    EXPECT_EQ(out.exchange_id, in.exchange_id);
    EXPECT_EQ(out.underlying, in.underlying);
    EXPECT_EQ(out.seq_num, in.seq_num);
    ASSERT_EQ(out.points.size(), in.points.size());
    for (std::size_t i = 0; i < in.points.size(); ++i) {
        const auto& a = in.points[i];
        const auto& b = out.points[i];
        EXPECT_EQ(b.instrument_id, a.instrument_id) << "i=" << i;
        EXPECT_EQ(b.expiry_date, a.expiry_date) << "i=" << i;
        EXPECT_DOUBLE_EQ(b.strike_price, a.strike_price) << "i=" << i;
        EXPECT_EQ(b.option_side, a.option_side) << "i=" << i;
        EXPECT_DOUBLE_EQ(b.implied_vol, a.implied_vol) << "i=" << i;
        EXPECT_DOUBLE_EQ(b.forward_price, a.forward_price) << "i=" << i;
        EXPECT_DOUBLE_EQ(b.time_to_expiry, a.time_to_expiry) << "i=" << i;
        EXPECT_DOUBLE_EQ(b.bid_iv, a.bid_iv) << "i=" << i;
        EXPECT_DOUBLE_EQ(b.ask_iv, a.ask_iv) << "i=" << i;
        EXPECT_DOUBLE_EQ(b.bid_price, a.bid_price) << "i=" << i;
        EXPECT_DOUBLE_EQ(b.ask_price, a.ask_price) << "i=" << i;
        EXPECT_DOUBLE_EQ(b.delta, a.delta) << "i=" << i;
        EXPECT_DOUBLE_EQ(b.gamma, a.gamma) << "i=" << i;
        EXPECT_DOUBLE_EQ(b.vega, a.vega) << "i=" << i;
        EXPECT_DOUBLE_EQ(b.theta, a.theta) << "i=" << i;
    }
}

TEST(SbeVolSurfaceCodec, RoundTripEmptyPoints) {
    SbeVolSurfaceCodec codec;
    const auto in = make_grid_with(0);

    alignas(8) std::array<std::byte, SbeVolSurfaceCodec::kRecommendedScratchSize> scratch{};
    const auto bytes = codec.encode(in, kTs, scratch);
    const auto out = codec.decode(bytes);

    EXPECT_EQ(out.underlying, in.underlying);
    EXPECT_EQ(out.seq_num, in.seq_num);
    EXPECT_TRUE(out.points.empty());
}

TEST(SbeVolSurfaceCodec, RoundTripMaxPoints) {
    // 400 points is the upper bound the previous publisher commented for.
    // Confirm we can encode + decode it without scratch overflow.
    SbeVolSurfaceCodec codec;
    const auto in = make_grid_with(400);

    alignas(8) std::array<std::byte, SbeVolSurfaceCodec::kRecommendedScratchSize> scratch{};
    const auto bytes = codec.encode(in, kTs, scratch);
    ASSERT_LE(bytes.size(), scratch.size());

    const auto out = codec.decode(bytes);
    EXPECT_EQ(out.points.size(), 400u);
    EXPECT_EQ(out.points.front().instrument_id, in.points.front().instrument_id);
    EXPECT_EQ(out.points.back().instrument_id, in.points.back().instrument_id);
}

TEST(SbeVolSurfaceCodec, DecodeRejectsTooShortBuffer) {
    SbeVolSurfaceCodec codec;
    std::array<std::byte, 4> too_short{};  // < MessageHeader::encodedLength()
    EXPECT_THROW(codec.decode(too_short), std::runtime_error);
}

TEST(SbeVolSurfaceCodec, DecodeRejectsWrongTemplateId) {
    SbeVolSurfaceCodec codec;
    // Build a buffer with a valid-shaped MessageHeader but a bogus
    // template id. The decoder should refuse rather than wrap an
    // arbitrary body type.
    alignas(8) std::array<std::byte, 32> buf{};
    auto* h = reinterpret_cast<uint16_t*>(buf.data());
    // SBE MessageHeader layout: blockLength, templateId, schemaId, version
    h[0] = 0;
    h[1] = 0xBEEF;  // not VolSurface::sbeTemplateId()
    h[2] = 0;
    h[3] = 0;
    EXPECT_THROW(codec.decode(buf), std::runtime_error);
}

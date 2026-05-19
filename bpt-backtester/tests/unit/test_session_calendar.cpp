#include "backtester/calendar/session_calendar.h"

#include <gtest/gtest.h>

using bpt::backtester::calendar::NamedDailyWindow;
using bpt::backtester::calendar::ResolvedWindow;
using bpt::backtester::calendar::SessionCalendar;

TEST(SessionCalendarTest, CryptoDefaultsRegisterFour) {
    auto cal = SessionCalendar::with_crypto_defaults();
    EXPECT_TRUE(cal.has("asian_open"));
    EXPECT_TRUE(cal.has("european_open"));
    EXPECT_TRUE(cal.has("us_open"));
    EXPECT_TRUE(cal.has("us_close"));
    EXPECT_FALSE(cal.has("does_not_exist"));
}

TEST(SessionCalendarTest, ResolvesUsOpenAcrossDates) {
    auto cal = SessionCalendar::with_crypto_defaults();
    const auto out = cal.resolve("us_open", {"2026-05-08", "2026-05-09"});
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].start, "2026-05-08T13:00:00Z");
    EXPECT_EQ(out[0].end, "2026-05-08T15:00:00Z");
    EXPECT_EQ(out[1].start, "2026-05-09T13:00:00Z");
    EXPECT_EQ(out[1].end, "2026-05-09T15:00:00Z");
}

TEST(SessionCalendarTest, ResolvesAsianOpenAtMidnight) {
    auto cal = SessionCalendar::with_crypto_defaults();
    const auto out = cal.resolve("asian_open", {"2026-01-01"});
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].start, "2026-01-01T00:00:00Z");
    EXPECT_EQ(out[0].end, "2026-01-01T02:00:00Z");
}

TEST(SessionCalendarTest, UnknownNameThrows) {
    auto cal = SessionCalendar::with_crypto_defaults();
    EXPECT_THROW(cal.resolve("fomc", {"2026-05-08"}), std::runtime_error);
}

TEST(SessionCalendarTest, AcceptsLongerIsoDates) {
    auto cal = SessionCalendar::with_crypto_defaults();
    // Caller may pass full ISO strings — only the date prefix is read.
    const auto out = cal.resolve("us_open", {"2026-05-08T17:00:00Z"});
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].start, "2026-05-08T13:00:00Z");
}

TEST(SessionCalendarTest, AddNamedRegistersCustomWindow) {
    SessionCalendar cal;
    cal.add_named({"fomc_window", 18 * 3600, 19 * 3600 + 30 * 60});
    EXPECT_TRUE(cal.has("fomc_window"));
    const auto out = cal.resolve("fomc_window", {"2026-06-18"});
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].start, "2026-06-18T18:00:00Z");
    EXPECT_EQ(out[0].end, "2026-06-18T19:30:00Z");
}

TEST(SessionCalendarTest, RejectsMalformedDate) {
    auto cal = SessionCalendar::with_crypto_defaults();
    EXPECT_THROW(cal.resolve("us_open", {"bad"}), std::runtime_error);
}

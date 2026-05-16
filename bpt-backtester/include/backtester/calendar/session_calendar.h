#pragma once

/// \file
/// \brief SessionCalendar — resolves named UTC daily windows for replay.

#include <chrono>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace bpt::backtester::calendar {

/// \brief One named UTC daily window.
///
/// start_offset_s and end_offset_s are seconds from midnight UTC of the
/// target date. Crypto markets are 24/7 so these describe *informationally
/// interesting* slices (cash-equity opens, macro release windows) rather
/// than exchange open/close.
struct NamedDailyWindow {
    std::string name;
    int32_t start_offset_s;  ///< seconds since 00:00 UTC.
    int32_t end_offset_s;    ///< seconds since 00:00 UTC, exclusive.
};

/// \brief Resolved window: half-open [start, end) ISO 8601 strings, ready to feed
///        into config::TimeWindow.
struct ResolvedWindow {
    std::string start;
    std::string end;
};

/// \brief SessionCalendar resolves named windows ("us_open", "asian_open", "fomc")
///        against a list of dates into TimeWindows that downstream code (DataLoader)
///        can consume directly via the Phase-2 simulation.windows mechanism.
///
/// Defaults are tuned for spot + perpetual crypto: even on 24/7 markets the
/// regional cash-equity opens dominate flow patterns and macro releases are
/// the largest scheduled volatility events.
class SessionCalendar {
public:
    /// \brief Standard crypto-relevant intraday windows. UTC.
    ///
    ///   asian_open      00:00–02:00  (Tokyo + early HK)
    ///   european_open   06:00–08:00  (London + Frankfurt)
    ///   us_open         13:00–15:00  (NY pre-mkt to first hour)
    ///   us_close        20:00–22:00  (NY close + after-hours leak)
    static SessionCalendar with_crypto_defaults() {
        SessionCalendar c;
        c.add_named({"asian_open", 0 * 3600, 2 * 3600});
        c.add_named({"european_open", 6 * 3600, 8 * 3600});
        c.add_named({"us_open", 13 * 3600, 15 * 3600});
        c.add_named({"us_close", 20 * 3600, 22 * 3600});
        return c;
    }

    void add_named(NamedDailyWindow w) { named_[w.name] = std::move(w); }

    /// \brief Resolves `name` over each date in `dates` into one TimeWindow per date.
    ///
    /// dates accepts the same forms as DataLoader: "YYYY-MM-DD" or full ISO.
    /// Throws if `name` is unknown.
    /// \param name  Named window key registered via add_named.
    /// \param dates Per-day base dates to resolve against.
    /// \return One ResolvedWindow per input date, in input order.
    std::vector<ResolvedWindow> resolve(const std::string& name, const std::vector<std::string>& dates) const {
        const auto it = named_.find(name);
        if (it == named_.end())
            throw std::runtime_error("SessionCalendar: unknown session name '" + name + "'");
        const auto& w = it->second;

        std::vector<ResolvedWindow> out;
        out.reserve(dates.size());
        for (const auto& d : dates)
            out.push_back(ResolvedWindow{format_offset(d, w.start_offset_s), format_offset(d, w.end_offset_s)});
        return out;
    }

    /// \brief Whether a name is registered. Useful for caller-side validation.
    bool has(const std::string& name) const noexcept { return named_.find(name) != named_.end(); }

private:
    /// \brief Returns "YYYY-MM-DDTHH:MM:SSZ" for `date_str`'s date plus offset_s seconds.
    ///
    /// date_str may be "YYYY-MM-DD" or any longer ISO 8601 form (the date prefix
    /// is what we read).
    static std::string format_offset(const std::string& date_str, int32_t offset_s) {
        if (date_str.size() < 10)
            throw std::runtime_error("SessionCalendar: bad date '" + date_str + "'");
        const std::string ymd = date_str.substr(0, 10);  // "YYYY-MM-DD"
        const int hh = (offset_s / 3600) % 24;
        const int mm = (offset_s / 60) % 60;
        const int ss = offset_s % 60;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%sT%02d:%02d:%02dZ", ymd.c_str(), hh, mm, ss);
        return std::string(buf);
    }

    std::unordered_map<std::string, NamedDailyWindow> named_;
};

}  // namespace bpt::backtester::calendar

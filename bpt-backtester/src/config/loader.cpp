#include "backtester/calendar/session_calendar.h"
#include "backtester/config/settings.h"

#include <algorithm>
#include <bpt_app/base_settings.h>
#include <bpt_common/logging.h>
#include <stdexcept>
#include <string_view>
#include <toml++/toml.hpp>
#include <type_traits>

namespace bpt::backtester::config {

namespace {

// One-call replacement for the `if (auto v = t["k"].value<T>()) field = *v;`
// idiom. Returns true when the key was present so callers that need to know
// (e.g. "did *any* latency field get set?") can still ask.
template <class T>
bool assign(const toml::table& t, std::string_view key, T& out) {
    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, bool> || std::is_same_v<T, double>) {
        if (auto v = t[key].value<T>()) {
            out = *v;
            return true;
        }
    } else if constexpr (std::is_integral_v<T>) {
        // toml++ exposes integers as int64_t; narrow casts here keep the
        // section parsers free of the static_cast<>() boilerplate they had
        // sprinkled across every uint16_t / uint32_t field.
        if (auto v = t[key].value<int64_t>()) {
            out = static_cast<T>(*v);
            return true;
        }
    } else {
        static_assert(sizeof(T) == 0, "assign() does not support this type — extend the dispatcher");
    }
    return false;
}

VenueLatencySpec parse_venue_spec(const toml::table& t) {
    VenueLatencySpec s{};
    assign(t, "submit_to_match_base_ns", s.submit_to_match_base_ns);
    assign(t, "submit_to_match_jitter_ns", s.submit_to_match_jitter_ns);
    assign(t, "match_to_report_base_ns", s.match_to_report_base_ns);
    assign(t, "match_to_report_jitter_ns", s.match_to_report_jitter_ns);
    return s;
}

// Pre-Phase-3 latency schema. Existed but no consumer ever read it, so users
// expecting latency to apply got zero. Translated onto the new per-venue
// submit_to_match leg so configs that *had* these set start actually feeling
// the latency they thought they had. match_to_report stays at zero — the
// legacy form didn't expose that leg. TODO: drop after 2026-Q3 once configs
// have migrated and the deprecation warning has stopped firing in CI.
void apply_legacy_latency(const toml::table& lat, SimLatencyConfig& out) {
    bool legacy_used = false;
    auto fill_if_unset = [&](const char* venue, uint64_t base_ns, uint64_t jitter_ns) {
        auto& spec = out.per_venue[venue];
        if (spec.submit_to_match_base_ns == 0 && base_ns > 0)
            spec.submit_to_match_base_ns = base_ns;
        if (spec.submit_to_match_jitter_ns == 0 && jitter_ns > 0)
            spec.submit_to_match_jitter_ns = jitter_ns;
    };
    int64_t cex_base_ms = 0;
    if (assign(lat, "cex_base_ms", cex_base_ms)) {
        legacy_used = true;
        const uint64_t ns = static_cast<uint64_t>(cex_base_ms) * 1'000'000ULL;
        fill_if_unset("BINANCE", ns, 0);
        fill_if_unset("OKX", ns, 0);
        fill_if_unset("DERIBIT", ns, 0);
    }
    int64_t hl_base_ms = 0;
    if (assign(lat, "hyperliquid_base_ms", hl_base_ms)) {
        legacy_used = true;
        fill_if_unset("HYPERLIQUID", static_cast<uint64_t>(hl_base_ms) * 1'000'000ULL, 0);
    }
    int64_t hl_jitter_ms = 0;
    if (assign(lat, "hyperliquid_jitter_ms", hl_jitter_ms)) {
        legacy_used = true;
        fill_if_unset("HYPERLIQUID", 0, static_cast<uint64_t>(hl_jitter_ms) * 1'000'000ULL);
    }
    if (legacy_used) {
        bpt::common::log::warn(
            "[config] simulation.latency: legacy ms fields are deprecated. "
            "Use [simulation.latency.<VENUE>] submit_to_match_base_ns / _jitter_ns / "
            "match_to_report_base_ns / _jitter_ns instead.");
    }
}

void parse_latency(const toml::table& lat, SimLatencyConfig& out) {
    assign(lat, "seed", out.seed);
    if (auto* d = lat["default"].as_table())
        out.default_spec = parse_venue_spec(*d);
    for (const char* venue : {"BINANCE", "OKX", "HYPERLIQUID", "DERIBIT"}) {
        if (auto* vt = lat[venue].as_table())
            out.per_venue[venue] = parse_venue_spec(*vt);
    }
    apply_legacy_latency(lat, out);
}

// Parse the simulation window source — exactly one of:
//   - top-level start / end (single window)
//   - [[simulation.windows]] (explicit list)
//   - [[simulation.sessions]] (resolved against the crypto session calendar)
// Throws on missing or conflicting specifications. Populates windows + the
// back-compat start/end scalars (sorted by start).
void parse_simulation_windows(const toml::table& sim, SimulationConfig& out) {
    auto top_start = sim["start"].value<std::string>();
    auto top_end = sim["end"].value<std::string>();
    auto* win_arr = sim["windows"].as_array();
    auto* sess_arr = sim["sessions"].as_array();

    const bool has_top = top_start.has_value() || top_end.has_value();
    const bool has_arr = win_arr != nullptr;
    const bool has_sess = sess_arr != nullptr;
    const int set_count = (has_top ? 1 : 0) + (has_arr ? 1 : 0) + (has_sess ? 1 : 0);
    if (set_count > 1)
        throw std::runtime_error(
            "simulation: pick exactly one of top-level start/end, "
            "[[simulation.windows]], or [[simulation.sessions]]");

    if (has_arr) {
        for (auto& elem : *win_arr) {
            auto* t = elem.as_table();
            if (!t)
                continue;
            TimeWindow w;
            assign(*t, "start", w.start);
            assign(*t, "end", w.end);
            if (w.start.empty() || w.end.empty())
                throw std::runtime_error("simulation.windows: every entry must have non-empty start and end");
            out.windows.push_back(std::move(w));
        }
        if (out.windows.empty())
            throw std::runtime_error("simulation.windows: array is empty");
    } else if (has_sess) {
        const auto cal = bpt::backtester::calendar::SessionCalendar::with_crypto_defaults();
        for (auto& elem : *sess_arr) {
            auto* t = elem.as_table();
            if (!t)
                continue;
            auto name = (*t)["name"].value<std::string>();
            auto* dates = (*t)["dates"].as_array();
            if (!name || !dates)
                throw std::runtime_error("simulation.sessions: each entry needs name and dates[]");
            std::vector<std::string> dlist;
            for (auto& dv : *dates)
                if (auto sv = dv.value<std::string>())
                    dlist.push_back(*sv);
            for (const auto& w : cal.resolve(*name, dlist))
                out.windows.push_back(TimeWindow{w.start, w.end});
        }
        if (out.windows.empty())
            throw std::runtime_error("simulation.sessions: produced no windows");
    } else if (top_start && top_end) {
        out.windows.push_back(TimeWindow{*top_start, *top_end});
    } else {
        throw std::runtime_error(
            "simulation: must specify top-level start/end, "
            "[[simulation.windows]], or [[simulation.sessions]]");
    }

    std::stable_sort(out.windows.begin(), out.windows.end(), [](const TimeWindow& a, const TimeWindow& b) {
        return a.start < b.start;
    });
    out.start = out.windows.front().start;
    out.end = out.windows.back().end;
}

void parse_simulation(const toml::table& sim, SimulationConfig& out) {
    parse_simulation_windows(sim, out);
    assign(sim, "allow_partial_data", out.allow_partial_data);
    assign(sim, "subscriber_wait_timeout_s", out.subscriber_wait_timeout_s);
    if (auto* lat = sim["latency"].as_table())
        parse_latency(*lat, out.latency);
}

void parse_data(const toml::table& d, DataConfig& out) {
    assign(d, "local_cache", out.local_cache);
    assign(d, "hyperliquid_refdata_snapshot", out.hyperliquid_refdata_snapshot);
}

void parse_endpoints(const toml::table& ep, EndpointConfig& out) {
    assign(ep, "binance_md_port", out.binance_md_port);
    assign(ep, "okx_md_port", out.okx_md_port);
    assign(ep, "hyperliquid_md_port", out.hyperliquid_md_port);
    assign(ep, "deribit_md_port", out.deribit_md_port);
    assign(ep, "binance_order_port", out.binance_order_port);
    assign(ep, "okx_order_port", out.okx_order_port);
    assign(ep, "hyperliquid_order_port", out.hyperliquid_order_port);
    assign(ep, "deribit_order_port", out.deribit_order_port);
    assign(ep, "hyperliquid_info_port", out.hyperliquid_info_port);
}

void parse_instruments(const toml::array& arr, std::vector<InstrumentConfig>& out) {
    for (auto& elem : arr) {
        auto* t = elem.as_table();
        if (!t)
            continue;
        InstrumentConfig ic;
        assign(*t, "exchange", ic.exchange);
        assign(*t, "symbol", ic.symbol);
        if (!ic.exchange.empty() && !ic.symbol.empty())
            out.push_back(std::move(ic));
    }
}

void parse_results(const toml::table& r, ResultsConfig& out, const std::vector<InstrumentConfig>& instruments) {
    assign(r, "output_dir", out.output_dir);
    assign(r, "starting_capital", out.starting_capital);

    // Per-venue fee table. Format:
    //   [results.fees.OKX] maker_bps = 2  taker_bps = 5
    //   [results.fees.HYPERLIQUID] maker_bps = -1.5  taker_bps = 4.5
    if (auto* fees = r["fees"].as_table()) {
        for (const auto& [venue, val] : *fees) {
            auto* vt = val.as_table();
            if (!vt)
                continue;
            ResultsConfig::FeeRates rates;
            assign(*vt, "maker_bps", rates.maker_bps);
            assign(*vt, "taker_bps", rates.taker_bps);
            out.fees_by_venue[std::string{venue.str()}] = rates;
        }
    }

    // Back-compat: old `fee_bps_per_fill` scalar is treated as both maker and
    // taker, applied to every venue in the run. Loud warning so configs get
    // migrated.
    double flat_bps = 0.0;
    if (assign(r, "fee_bps_per_fill", flat_bps)) {
        for (const auto& inst : instruments) {
            auto& rates = out.fees_by_venue[inst.exchange];
            rates.maker_bps = flat_bps;
            rates.taker_bps = flat_bps;
        }
    }
}

void parse_aeron_stream(const toml::table& t, bpt::common::config::StreamConfig& out) {
    assign(t, "channel", out.channel);
    assign(t, "stream_id", out.stream_id);
}

void parse_aeron(const toml::table& a, AeronConfig& out) {
    if (auto* t = a["backtest_control"].as_table())
        parse_aeron_stream(*t, out.backtest_control);
    if (auto* t = a["backtest_ack"].as_table())
        parse_aeron_stream(*t, out.backtest_ack);
}

}  // namespace

Settings load(const std::string& path) {
    toml::table root = toml::parse_file(path);

    Settings s;
    bpt::app::load_base_settings(root, s.base);

    if (auto* sim = root["simulation"].as_table())
        parse_simulation(*sim, s.simulation);
    if (auto* d = root["data"].as_table())
        parse_data(*d, s.data);
    if (auto* ep = root["endpoints"].as_table())
        parse_endpoints(*ep, s.endpoints);
    if (auto* arr = root["instruments"].as_array())
        parse_instruments(*arr, s.instruments);
    if (auto* r = root["results"].as_table())
        parse_results(*r, s.results, s.instruments);
    if (auto* a = root["aeron"].as_table())
        parse_aeron(*a, s.aeron);

    return s;
}

}  // namespace bpt::backtester::config

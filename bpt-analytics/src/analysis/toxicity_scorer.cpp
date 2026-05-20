#include "analytics/analysis/toxicity_scorer.h"

#include <cmath>

namespace bpt::analytics::analysis {

ToxicityScorer::ToxicityScorer(Config cfg) : cfg_(cfg) {}

void ToxicityScorer::add(const MarkoutTracker::Observation& obs) {
    window_.push_back(obs);

    // Evict by size
    while (window_.size() > cfg_.window_size)
        window_.pop_front();

    // Evict by duration
    if (cfg_.window_duration_ns > 0 && !window_.empty()) {
        const uint64_t latest = window_.back().fill_ts_ns;
        while (!window_.empty() && latest - window_.front().fill_ts_ns > cfg_.window_duration_ns)
            window_.pop_front();
    }
}

ToxicityScorer::SideStats ToxicityScorer::compute_side(int side_sign, uint64_t instrument_id) const {
    SideStats stats;
    double sum_markout_1s = 0.0;
    double sum_markout_5s = 0.0;
    double sum_markout_30s = 0.0;
    int adverse_count = 0;
    int count = 0;

    for (const auto& obs : window_) {
        if (obs.side_sign != side_sign)
            continue;
        if (instrument_id != 0 && obs.instrument_id != instrument_id)
            continue;

        sum_markout_1s += obs.markout_1s_bps;
        sum_markout_5s += obs.markout_5s_bps;
        sum_markout_30s += obs.markout_30s_bps;
        // adverse_rate is defined off the 5s horizon — the canonical
        // microstructure-toxicity convention. Don't change it just
        // because we now also report 1s/30s means.
        if (obs.markout_5s_bps < 0.0)
            ++adverse_count;
        ++count;
    }

    stats.count = static_cast<uint32_t>(count);
    if (count == 0) {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        stats.mean_markout_1s_bps = nan;
        stats.mean_markout_5s_bps = nan;
        stats.mean_markout_30s_bps = nan;
        stats.adverse_rate = nan;
        stats.toxicity_score = nan;
        return stats;
    }

    stats.mean_markout_1s_bps = sum_markout_1s / count;
    stats.mean_markout_5s_bps = sum_markout_5s / count;
    stats.mean_markout_30s_bps = sum_markout_30s / count;
    stats.adverse_rate = static_cast<double>(adverse_count) / count;
    // Toxicity score: mean 5s markout scaled by adverse rate.
    // Both negative markout AND high adverse rate contribute to a more
    // negative score. A side with -3 bps mean and 80% adverse is more
    // toxic than one with -3 bps mean and 40% adverse.
    stats.toxicity_score = stats.mean_markout_5s_bps * (0.5 + stats.adverse_rate);

    return stats;
}

messaging::ToxicityUpdate ToxicityScorer::compute(uint64_t instrument_id, uint64_t now_ns) const {
    const auto bid = compute_side(+1, instrument_id);
    const auto ask = compute_side(-1, instrument_id);

    messaging::ToxicityUpdate update{};
    update.instrument_id = instrument_id;
    update.timestamp_ns = now_ns;

    const double nan = std::numeric_limits<double>::quiet_NaN();

    if (bid.count >= cfg_.min_samples) {
        update.bid_markout_1s_bps = bid.mean_markout_1s_bps;
        update.bid_markout_5s_bps = bid.mean_markout_5s_bps;
        update.bid_markout_30s_bps = bid.mean_markout_30s_bps;
        update.bid_adverse_rate = bid.adverse_rate;
        update.bid_toxicity_score = bid.toxicity_score;
        update.bid_sample_count = bid.count;
    } else {
        update.bid_markout_1s_bps = nan;
        update.bid_markout_5s_bps = nan;
        update.bid_markout_30s_bps = nan;
        update.bid_adverse_rate = nan;
        update.bid_toxicity_score = nan;
        update.bid_sample_count = bid.count;
    }

    if (ask.count >= cfg_.min_samples) {
        update.ask_markout_1s_bps = ask.mean_markout_1s_bps;
        update.ask_markout_5s_bps = ask.mean_markout_5s_bps;
        update.ask_markout_30s_bps = ask.mean_markout_30s_bps;
        update.ask_adverse_rate = ask.adverse_rate;
        update.ask_toxicity_score = ask.toxicity_score;
        update.ask_sample_count = ask.count;
    } else {
        update.ask_markout_1s_bps = nan;
        update.ask_markout_5s_bps = nan;
        update.ask_markout_30s_bps = nan;
        update.ask_adverse_rate = nan;
        update.ask_toxicity_score = nan;
        update.ask_sample_count = ask.count;
    }

    return update;
}

}  // namespace bpt::analytics::analysis

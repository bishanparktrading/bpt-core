#include "features/ewma.h"

#include <cmath>

namespace bpt::features {

void TimeWeightedEwma::update(double obs, double dt_s) {
    if (dt_s <= 0.0)
        return;
    const double lambda = std::exp(-dt_s / halflife_s_);
    value_ = lambda * value_ + (1.0 - lambda) * obs;
    ++count_;
}

void TimeWeightedEwma::reset() {
    value_ = 0.0;
    count_ = 0;
}

void EwmaVariance::update(double mid, uint64_t ts_ns) {
    if (last_mid_ > 0.0 && ts_ns > last_ns_) {
        const double dt_s = static_cast<double>(ts_ns - last_ns_) * 1e-9;
        if (dt_s > 0.0) {
            const double log_ret = std::log(mid / last_mid_);
            const double norm_ret = log_ret / std::sqrt(dt_s);
            ewma_.update(norm_ret * norm_ret, dt_s);
        }
    }
    last_mid_ = mid;
    last_ns_ = ts_ns;
}

void EwmaVariance::reset() {
    ewma_.reset();
    last_mid_ = 0.0;
    last_ns_ = 0;
}

void EwmaDrift::update(double mid, uint64_t ts_ns) {
    if (last_mid_ > 0.0 && ts_ns > last_ns_) {
        const double dt_s = static_cast<double>(ts_ns - last_ns_) * 1e-9;
        if (dt_s > 0.0) {
            const double log_ret = std::log(mid / last_mid_);
            const double norm_ret = log_ret / std::sqrt(dt_s);
            ewma_.update(norm_ret, dt_s);
        }
    }
    last_mid_ = mid;
    last_ns_ = ts_ns;
}

void EwmaDrift::reset() {
    ewma_.reset();
    last_mid_ = 0.0;
    last_ns_ = 0;
}

void KappaEstimator::update(uint64_t trade_ts_ns) {
    if (last_trade_ns_ > 0 && trade_ts_ns > last_trade_ns_) {
        const double dt_s = static_cast<double>(trade_ts_ns - last_trade_ns_) * 1e-9;
        if (dt_s > 0.0) {
            const double arrival_rate = 0.5 / dt_s;
            ewma_.update(arrival_rate, dt_s);
        }
    }
    last_trade_ns_ = trade_ts_ns;
}

void KappaEstimator::reset() {
    ewma_.reset();
    last_trade_ns_ = 0;
}

void EwmaVariance::restore(const Snapshot& s) {
    ewma_.restore(s.value, s.count);
    last_mid_ = s.last_mid;
    last_ns_ = s.last_ns;
}

void EwmaDrift::restore(const Snapshot& s) {
    ewma_.restore(s.value, s.count);
    last_mid_ = s.last_mid;
    last_ns_ = s.last_ns;
}

void KappaEstimator::restore(const Snapshot& s) {
    ewma_.restore(s.value, s.count);
    last_trade_ns_ = s.last_trade_ns;
}

}  // namespace bpt::features

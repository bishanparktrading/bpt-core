#pragma once

// No-op metrics stub — used until prometheus-cpp is available in fenrir's vcpkg.
// All calls are inline no-ops; no HTTP server is started.
// Replace with the real implementation once prometheus-cpp is installed.

#include <string>

namespace fenrir::metrics {

struct NoOp {
    void Set(double) {}
    void Increment(double = 1.0) {}
    void Observe(double) {}
};

struct FenrirMetrics {
    NoOp _noop;

    NoOp* healthy = &_noop;
    NoOp* strategy_active = &_noop;
    NoOp* trading_paused = &_noop;
    NoOp* md_ticks_total = &_noop;
    NoOp* exec_reports_total = &_noop;

    explicit FenrirMetrics(int /*port*/) {}

    void shutdown() {}

    NoOp& refdata_ready(const std::string&) { return _noop; }
    NoOp& orders_sent(const std::string&) { return _noop; }
};

}  // namespace fenrir::metrics

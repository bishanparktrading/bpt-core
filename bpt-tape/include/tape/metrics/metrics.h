#pragma once

/// @file
/// Prometheus metrics exposed by bpt-tape on metrics_host:metrics_port.
///
/// Mirrors the StrategyMetrics / OrderGatewayMetrics shape: registry +
/// exposer + cached metric pointers, no per-call allocation on the hot
/// path. RawSpool gets venue-labeled hook lambdas via hooks_for() so the
/// recording library (bpt-common) stays free of any prometheus-cpp
/// dependency.
///
/// Why these specific metrics:
///   bpt_tape_last_wslog_write_unix_seconds  the alert that would have
///       caught the 2026-05-09 ENOSPC incident at minute zero. Set on
///       every successful write_record(); compare against time() in
///       Alertmanager.
///   bpt_tape_frames_written_total           the "is data flowing" counter.
///   bpt_tape_bytes_written_total            data-rate panel input. Catches
///       the May-8 6× regression (T87) had it existed.
///   bpt_tape_wslog_rotations_total          counts hourly file roll-overs.
///   bpt_tape_wslog_rotation_failures_total  first-class signal for the
///       silent-failure mode RawSpool now logs+aborts on.

#include "bpt_common/recorder/raw_spool.h"

#include <memory>
#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/family.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>
#include <string>

namespace bpt::tape::metrics {

class TapeMetrics {
public:
    /// Binds an HTTP exposer at host:port. host=0.0.0.0 in prod so the
    /// in-VPC monitor host can scrape; the tape-host SG restricts which
    /// peer can actually reach the port.
    TapeMetrics(const std::string& host, uint16_t port);

    /// Hook bundle for a single RawSpool. Lambdas capture the labeled
    /// metric refs once (resolved via Family::Add()) so the hot path
    /// is a single virtual call + atomic increment, not a hash lookup.
    /// Caller installs these into RawSpool::Config::metrics.
    bpt::common::recorder::RawSpool::MetricsHooks hooks_for(const std::string& venue);

    /// WebSocket connection state callbacks for the recording mdgw
    /// adapter's on_connect/on_disconnect lambdas. ws_connected is a
    /// gauge (current state); ws_reconnects_total is a counter (how
    /// often it has flapped). Together they distinguish "down right
    /// now" from "down 4x in the last hour."
    void on_ws_connect(const std::string& venue);
    void on_ws_disconnect(const std::string& venue);

    /// Set the count of currently-subscribed instruments per venue.
    /// Called once after the universe loads at startup. Catches the
    /// "did the universe shrink unexpectedly" failure class.
    void set_subscriptions(const std::string& venue, std::size_t count);

    /// Flip the healthy gauge to 0 on shutdown so dashboards can
    /// distinguish "process down" from "process up but stale".
    void shutdown();

private:
    std::shared_ptr<prometheus::Registry> registry_;
    std::unique_ptr<prometheus::Exposer> exposer_;

    // Liveness / state
    prometheus::Gauge* healthy_{};

    // Per-venue families. Lookups happen once per spool at construct time.
    prometheus::Family<prometheus::Gauge>*   last_wslog_write_unix_seconds_fam_{};
    prometheus::Family<prometheus::Counter>* frames_written_total_fam_{};
    prometheus::Family<prometheus::Counter>* bytes_written_total_fam_{};
    prometheus::Family<prometheus::Counter>* wslog_rotations_total_fam_{};
    prometheus::Family<prometheus::Counter>* wslog_rotation_failures_total_fam_{};
    prometheus::Family<prometheus::Gauge>*   ws_connected_fam_{};
    prometheus::Family<prometheus::Counter>* ws_reconnects_total_fam_{};
    prometheus::Family<prometheus::Gauge>*   subscriptions_fam_{};
};

}  // namespace bpt::tape::metrics

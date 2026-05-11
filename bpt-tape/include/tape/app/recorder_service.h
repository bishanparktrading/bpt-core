#pragma once

/// \file
/// \brief bpt-tape's IService implementation; owns the recording stack.
///
/// One RecorderService is constructed at startup and handed to
/// bpt::app::run. It owns the per-venue WS recording adapters, the REST
/// refdata pollers, and the Prometheus metrics exposer. Keeping the
/// logic out of main.cpp lets us unit-test the service without booting
/// the bpt-app framework.
///
/// Threading: built and destroyed from the main thread; adapter and
/// poller threads are owned by their respective objects, joined by
/// stop(). TapeMetrics is constructed before any spool and destroyed
/// after — spool metrics-hook lambdas capture refs into its prometheus
/// families, so member declaration order is load-bearing.

#include "bpt_common/recorder/raw_spool.h"
#include "md_gateway/adapter/common/i_adapter.h"
#include "tape/config/settings.h"
#include "tape/metrics/metrics.h"
#include "tape/refdata/refdata_poller.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <bpt_app/app.h>
#include <bpt_common/util/topology.h>

namespace bpt::tape::app {

/// \brief Recording service: WS adapters + REST pollers + metrics exposer.
///
/// Construction wires every venue; run() blocks on the framework signal
/// loop; stop() quiesces adapter threads, writes SESSION_STOP markers,
/// and flips the healthy gauge to 0 (lets dashboards tell "clean stop"
/// from "crash" — see metrics::TapeMetrics).
class RecorderService : public bpt::app::IService {
public:
    /// \throws std::runtime_error if a configured venue has no recording
    ///         adapter implementation, or if the instrument-mapping JSON
    ///         can't be loaded. Adapter threads that started before the
    ///         throw are stopped before the exception propagates.
    RecorderService(config::Settings settings,
                    const bpt::common::util::Topology& topology);

    /// \brief Block on bpt::common::signal until SIGINT/SIGTERM.
    void run() override;

    /// \brief Quiesce adapters + pollers, write SESSION_STOP, flip healthy=0.
    void stop() override;

private:
    /// \brief Per-spool state shared between on_connect / on_disconnect
    ///        so the first connect can be skipped (covered by SESSION_START
    ///        already) and reconnects get a numbered WS_RECONNECT marker.
    struct ConnState {
        bool was_disconnected{false};
        uint32_t reconnect_count{0};
    };

    // Constructor phases — named so the ctor body reads as orchestration.
    void setup_metrics();
    void setup_mdgw_recording();
    void setup_universe();
    void setup_refdata_pollers();

    /// \brief Build a RawSpool from settings_.recording, wiring per-venue
    ///        metrics hooks if metrics_ is live.
    std::shared_ptr<bpt::common::recorder::RawSpool> make_spool(
        const std::string& venue_tag);

    /// \brief Install on_connect / on_disconnect callbacks on the adapter
    ///        that (1) emit WS_RECONNECT / WS_DISCONNECT spool markers,
    ///        (2) drive ws_connected + ws_reconnects_total metrics.
    void wire_connection_markers(
        std::shared_ptr<bpt::md_gateway::adapter::IAdapter> adapter,
        std::shared_ptr<bpt::common::recorder::RawSpool> spool,
        const std::string& venue_tag);

    config::Settings settings_;
    const bpt::common::util::Topology& topology_;

    /// Built before any spool, destroyed after every spool — the
    /// metrics-hook lambdas captured by spools hold references into
    /// TapeMetrics-owned prometheus families.
    std::unique_ptr<metrics::TapeMetrics> metrics_;

    std::vector<std::shared_ptr<bpt::common::recorder::RawSpool>> spools_;
    std::vector<std::shared_ptr<bpt::md_gateway::adapter::IAdapter>> adapters_;
    std::unordered_map<std::string,
                       std::shared_ptr<bpt::md_gateway::adapter::IAdapter>>
        adapters_per_venue_;

    std::vector<std::shared_ptr<bpt::common::recorder::RawSpool>> refdata_spools_;
    std::vector<std::unique_ptr<refdata::RefdataPoller>> refdata_pollers_;
};

}  // namespace bpt::tape::app

#pragma once

/// @file
/// Recording subclasses of the bpt-md-gateway venue adapters. Each overrides
/// handle_frame() to tee the raw WS payload to a shared RawSpool BEFORE
/// calling the parent's handle_frame() — preserves the existing frame-queue
/// + parser pipeline while adding the recording tap. The mdgw adapter
/// source is untouched; recording is a bpt-tape-only concern.
///
/// handle_frame is the IO-thread seam invoked by the venue's MdWsClient
/// for each application frame (post protocol-level filtering — no
/// keepalive noise reaches the spool).
///
/// Templated on Pub (the inner publisher type) — bpt-tape instantiates
/// each adapter with NoopMdPublisher so the publish() chain compiles down
/// to dead branches the optimizer can drop.

#include "bpt_common/recorder/raw_spool.h"
#include "md_gateway/adapter/binance/binance_md_adapter.h"
#include "md_gateway/adapter/deribit/deribit_md_adapter.h"
#include "md_gateway/adapter/hyperliquid/hyperliquid_md_adapter.h"
#include "md_gateway/adapter/okx/okx_md_adapter.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>

namespace bpt::tape::adapter {

#define BPT_DECLARE_RECORDING_ADAPTER(Class, BaseClass)                                       \
    template <class Pub>                                                                      \
    class Class : public ::bpt::md_gateway::adapter::BaseClass<Pub> {                         \
    public:                                                                                   \
        Class(std::shared_ptr<::bpt::common::recorder::RawSpool> spool,                       \
              const ::bpt::md_gateway::config::AdapterConfig& cfg,                            \
              std::shared_ptr<Pub> md_pub)                                                    \
            : ::bpt::md_gateway::adapter::BaseClass<Pub>(cfg, std::move(md_pub)),             \
              spool_(std::move(spool)) {}                                                     \
                                                                                              \
    protected:                                                                                \
        void handle_frame(std::string_view payload, uint64_t recv_ns) noexcept override {    \
            if (spool_ && !spool_->write_frame(recv_ns, payload)) {                           \
                /* RawSpool already logged the cause via Quill (async). Write a   */         \
                /* synchronous stderr line too so journald captures the fatal     */         \
                /* even if Quill's async queue doesn't drain before abort().      */         \
                std::fputs("[FATAL] bpt-tape: RawSpool::write_frame failed; "                 \
                           "aborting (Restart=always recycles).\n", stderr);                  \
                std::fflush(stderr);                                                          \
                std::abort();                                                                 \
            }                                                                                 \
            ::bpt::md_gateway::adapter::BaseClass<Pub>::handle_frame(payload, recv_ns);       \
        }                                                                                     \
                                                                                              \
    private:                                                                                  \
        std::shared_ptr<::bpt::common::recorder::RawSpool> spool_;                            \
    };

BPT_DECLARE_RECORDING_ADAPTER(RecordingBinanceMdAdapter, BinanceMdAdapter)
BPT_DECLARE_RECORDING_ADAPTER(RecordingOkxMdAdapter, OkxMdAdapter)
BPT_DECLARE_RECORDING_ADAPTER(RecordingHyperliquidMdAdapter, HyperliquidMdAdapter)
BPT_DECLARE_RECORDING_ADAPTER(RecordingDeribitMdAdapter, DeribitMdAdapter)

#undef BPT_DECLARE_RECORDING_ADAPTER

}  // namespace bpt::tape::adapter

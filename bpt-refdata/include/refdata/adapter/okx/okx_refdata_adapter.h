#pragma once

/// \file
/// \brief OKX REST reference-data adapter.

#include "refdata/adapter/common/i_exchange_refdata_adapter.h"
#include "refdata/adapter/credentials.h"
#include "refdata/adapter/okx/okx_refdata_decoder.h"
#include "refdata/config/settings.h"
#include "refdata/http/rest_client.h"
#include "refdata/mapping/instrument_mapping_loader.h"
#include "refdata/registry/instrument_registry.h"

#include <atomic>
#include <memory>

namespace bpt::refdata::adapter {

/// \brief Pulls OKX SPOT + SWAP instruments and fee schedule from REST.
///
/// Snapshot (blocking, called on startup):
///   - `GET /api/v5/public/instruments?instType=SPOT` — spot instruments
///   - `GET /api/v5/public/instruments?instType=SWAP` — perpetual swaps
///   - `GET /api/v5/account/trade-fee` — fee schedules (requires API key)
///
/// Hourly poll re-fetches the instruments endpoints to detect listing
/// changes. Funding rates flow on the MdGateway side (OKX funding-rate
/// WS channel) — this adapter does not stream them.
class OKXRefDataAdapter : public IExchangeRefDataAdapter {
public:
    OKXRefDataAdapter(const config::AdapterConfig& cfg,
                      const ExchangeCredentials& creds,
                      std::shared_ptr<registry::InstrumentRegistry> registry,
                      std::shared_ptr<mapping::InstrumentMappingLoader> mapping,
                      std::shared_ptr<http::RestClient> client);
    ~OKXRefDataAdapter() override = default;

    void fetchSnapshot() override;
    void subscribeDeltas() override {}  // no-op: funding rates in MdGateway
    void fetchInstrumentListing() override;
    void stop() override {}  // no-op: no WS thread

    bool isReady() const override { return ready_.load(std::memory_order_acquire); }
    const char* exchange_name() const override { return "OKX"; }
    bpt::messages::ExchangeId::Value exchange_id() const override { return bpt::messages::ExchangeId::OKX; }

private:
    config::AdapterConfig cfg_;
    std::shared_ptr<registry::InstrumentRegistry> registry_;
    std::atomic<bool> ready_{false};

    std::string api_key_;
    std::string secret_key_;
    std::string passphrase_;

    std::shared_ptr<http::RestClient> client_;
    OKXRefdataDecoder decoder_;
};

}  // namespace bpt::refdata::adapter

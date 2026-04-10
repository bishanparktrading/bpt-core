#pragma once

#include "muninn/adapter/common/i_exchange_refdata_adapter.h"
#include "muninn/adapter/credentials.h"
#include "muninn/adapter/okx/okx_parser.h"
#include "muninn/config/settings.h"
#include "muninn/http/rest_client.h"
#include "muninn/mapping/instrument_mapping_loader.h"
#include "muninn/registry/instrument_registry.h"

#include <atomic>
#include <memory>

namespace muninn::adapter {

// OKX REST reference data adapter.
//
// Snapshot (blocking, called on startup):
//   GET /api/v5/public/instruments?instType=SPOT  — spot instruments
//   GET /api/v5/public/instruments?instType=SWAP  — perpetual swap instruments
//   GET /api/v5/account/trade-fee                 — fee schedules (requires API key)
//
// Funding rates have moved to Huginn (OKX funding-rate WS channel).
//
// Hourly poll:
//   Re-fetches instruments endpoints to detect listing changes.
class OKXRefDataAdapter : public IExchangeRefDataAdapter {
public:
    OKXRefDataAdapter(const config::AdapterConfig& cfg,
                      const ExchangeCredentials& creds,
                      std::shared_ptr<registry::InstrumentRegistry> registry,
                      std::shared_ptr<mapping::InstrumentMappingLoader> mapping);
    ~OKXRefDataAdapter() override = default;

    void fetchSnapshot() override;
    void subscribeDeltas() override {}  // no-op: funding rates in Huginn
    void fetchInstrumentListing() override;
    void stop() override {}  // no-op: no WS thread

    bool isReady() const override { return ready_.load(std::memory_order_acquire); }
    const char* exchange_name() const override { return "OKX"; }
    bifrost::protocol::ExchangeId::Value exchange_id() const override { return bifrost::protocol::ExchangeId::OKX; }

private:
    config::AdapterConfig cfg_;
    std::shared_ptr<registry::InstrumentRegistry> registry_;
    std::atomic<bool> ready_{false};

    std::string api_key_;
    std::string secret_key_;
    std::string passphrase_;

    http::RestClient client_;
    OKXParser parser_;
};

}  // namespace muninn::adapter

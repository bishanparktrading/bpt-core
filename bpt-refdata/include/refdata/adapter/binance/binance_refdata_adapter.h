#pragma once

/// \file
/// \brief Binance REST reference-data adapter (spot + fapi + tradeFee).

#include "refdata/adapter/binance/binance_refdata_decoder.h"
#include "refdata/adapter/common/i_exchange_refdata_adapter.h"
#include "refdata/adapter/credentials.h"
#include "refdata/config/settings.h"
#include "refdata/http/rest_client.h"
#include "refdata/mapping/instrument_mapping_loader.h"
#include "refdata/registry/instrument_registry.h"

#include <atomic>
#include <memory>

namespace bpt::refdata::adapter {

/// \brief Pulls Binance SPOT + FUTURES instruments and fee schedule from REST.
///
/// Holds two RestClients because Binance hosts SPOT and FUTURES on
/// different domains (api.binance.com vs fapi.binance.com).
///
/// Snapshot (blocking, called on startup):
///   - `GET /api/v3/exchangeInfo` (spot_client_) — spot instruments
///   - `GET /fapi/v1/exchangeInfo` (fapi_client_) — futures / perp
///   - `GET /sapi/v1/asset/tradeFee` (spot_client_) — fees (needs API key)
///
/// Hourly poll re-fetches both exchangeInfo endpoints to detect listing
/// changes. Funding rates flow on the MdGateway side (Binance
/// !markPrice@arr@1s WS) — not this adapter's concern.
class BinanceRefDataAdapter : public IExchangeRefDataAdapter {
public:
    BinanceRefDataAdapter(const config::AdapterConfig& cfg,
                          const ExchangeCredentials& creds,
                          std::shared_ptr<registry::InstrumentRegistry> registry,
                          std::shared_ptr<mapping::InstrumentMappingLoader> mapping,
                          std::shared_ptr<http::RestClient> spot_client,
                          std::shared_ptr<http::RestClient> fapi_client);
    ~BinanceRefDataAdapter() override = default;

    void fetchSnapshot() override;
    void subscribeDeltas() override {}  // no-op: funding rates in MdGateway
    void fetchInstrumentListing() override;
    void stop() override {}  // no-op: no WS thread

    bool isReady() const override { return ready_.load(std::memory_order_acquire); }
    const char* exchange_name() const override { return "BINANCE"; }
    bpt::messages::ExchangeId::Value exchange_id() const override { return bpt::messages::ExchangeId::BINANCE; }

private:
    config::AdapterConfig cfg_;
    std::shared_ptr<registry::InstrumentRegistry> registry_;
    std::atomic<bool> ready_{false};

    std::string api_key_;

    std::shared_ptr<http::RestClient> spot_client_;
    std::shared_ptr<http::RestClient> fapi_client_;
    BinanceRefdataDecoder decoder_;
};

}  // namespace bpt::refdata::adapter

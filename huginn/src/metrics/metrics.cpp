#include "huginn/metrics/metrics.h"

namespace huginn::metrics {

HuginnMetrics::HuginnMetrics(const std::string& host, uint16_t port) {
    registry = std::make_shared<prometheus::Registry>();

    exposer = std::make_unique<prometheus::Exposer>(host + ":" + std::to_string(port));
    exposer->RegisterCollectable(registry);

    auto& h = prometheus::BuildGauge().Name("huginn_healthy").Help("1 if Huginn is running").Register(*registry);
    healthy = &h.Add({});
    healthy->Set(1.0);

    auto& sb = prometheus::BuildCounter()
                   .Name("huginn_subscription_batches_total")
                   .Help("Total MdSubscribeBatch messages received from Fenrir")
                   .Register(*registry);
    subscription_batches_total = &sb.Add({});

    auto& shb = prometheus::BuildCounter()
                    .Name("huginn_service_heartbeats_total")
                    .Help("Total MdServiceHeartbeat messages published")
                    .Register(*registry);
    service_heartbeats_total = &shb.Add({});

    auto& dr = prometheus::BuildGauge()
                   .Name("huginn_md_messages_dropped")
                   .Help("Cumulative MD messages dropped due to Aeron backpressure")
                   .Register(*registry);
    md_messages_dropped = &dr.Add({});

    exchange_connected_fam = &prometheus::BuildGauge()
                                  .Name("huginn_exchange_connected")
                                  .Help("1 if the adapter is connected to this exchange")
                                  .Register(*registry);

    adapter_disconnects_fam = &prometheus::BuildCounter()
                                   .Name("huginn_adapter_disconnects_total")
                                   .Help("Total unexpected disconnects per exchange adapter")
                                   .Register(*registry);

    md_messages_published_fam = &prometheus::BuildGauge()
                                     .Name("huginn_md_messages_published_total")
                                     .Help("Total MD messages published per exchange (monotonic)")
                                     .Register(*registry);

    md_validation_drops_fam = &prometheus::BuildGauge()
                                   .Name("huginn_md_validation_drops_total")
                                   .Help("Total MD messages dropped by MdValidator per exchange")
                                   .Register(*registry);

    decode_latency_p50_fam = &prometheus::BuildGauge()
                                  .Name("huginn_bbo_decode_p50_ns")
                                  .Help("BBO decode latency p50 (ns) per exchange")
                                  .Register(*registry);
    decode_latency_p95_fam = &prometheus::BuildGauge()
                                  .Name("huginn_bbo_decode_p95_ns")
                                  .Help("BBO decode latency p95 (ns) per exchange")
                                  .Register(*registry);
    decode_latency_p99_fam = &prometheus::BuildGauge()
                                  .Name("huginn_bbo_decode_p99_ns")
                                  .Help("BBO decode latency p99 (ns) per exchange")
                                  .Register(*registry);
    decode_latency_p999_fam = &prometheus::BuildGauge()
                                   .Name("huginn_bbo_decode_p999_ns")
                                   .Help("BBO decode latency p99.9 (ns) per exchange")
                                   .Register(*registry);
    decode_latency_max_fam = &prometheus::BuildGauge()
                                  .Name("huginn_bbo_decode_max_ns")
                                  .Help("BBO decode latency max (ns) per exchange")
                                  .Register(*registry);
    decode_latency_mean_fam = &prometheus::BuildGauge()
                                   .Name("huginn_bbo_decode_mean_ns")
                                   .Help("BBO decode latency mean (ns) per exchange")
                                   .Register(*registry);
}

}  // namespace huginn::metrics

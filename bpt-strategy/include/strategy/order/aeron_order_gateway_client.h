#pragma once

/// @file
/// AeronOrderGatewayClient<Handler> — Aeron-backed production client.
/// Templated on the Handler that receives parsed inbound events. In prod
/// the Handler is `StrategyService` and the per-message path is direct
/// (no `std::function` indirection).

#include "strategy/order/i_order_gateway_client.h"

#include <Aeron.h>
#include <FragmentAssembler.h>

#include <messages/AccountSnapshot.h>
#include <messages/AccountSnapshotRequest.h>
#include <messages/CancelAll.h>
#include <messages/CancelOrder.h>
#include <messages/ExecutionReport.h>
#include <messages/MessageHeader.h>
#include <messages/ModifyOrder.h>
#include <messages/NewOrder.h>
#include <messages/OrderGatewayHeartbeat.h>

#include <bpt_common/aeron/publisher.h>
#include <bpt_common/aeron/subscriber.h>
#include <bpt_common/logging.h>
#include <bpt_common/util/tsc_clock.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace bpt::strategy::order {

template <class Handler>
class AeronOrderGatewayClient final : public IOrderGatewayClient {
public:
    AeronOrderGatewayClient(std::shared_ptr<aeron::Aeron> aeron,
                            const std::string& channel,
                            int order_stream,
                            int exec_report_stream,
                            int heartbeat_stream,
                            int account_snapshot_stream) {
        order_pub_ = std::make_unique<bpt::common::aeron::Publisher>(
            aeron, channel, order_stream, bpt::common::aeron::Publisher::Policy::kRetryOnBackpressure);
        exec_report_sub_ = std::make_unique<bpt::common::aeron::Subscriber>(
            aeron,
            channel,
            exec_report_stream,
            [this](aeron::AtomicBuffer& buf,
                   aeron::util::index_t offset,
                   aeron::util::index_t length,
                   aeron::Header& hdr) { handle_exec_report_fragment(buf, offset, length, hdr); });
        heartbeat_sub_ = std::make_unique<bpt::common::aeron::Subscriber>(
            aeron,
            channel,
            heartbeat_stream,
            [this](aeron::AtomicBuffer& buf,
                   aeron::util::index_t offset,
                   aeron::util::index_t length,
                   aeron::Header& hdr) { handle_heartbeat_fragment(buf, offset, length, hdr); });

        if (account_snapshot_stream != 0) {
            account_snapshot_sub_ = std::make_unique<bpt::common::aeron::Subscriber>(
                aeron,
                channel,
                account_snapshot_stream,
                [this](aeron::AtomicBuffer& buf,
                       aeron::util::index_t offset,
                       aeron::util::index_t length,
                       aeron::Header& hdr) { handle_account_snapshot_fragment(buf, offset, length, hdr); });
        }
    }

    void set_handler(Handler* handler) noexcept { handler_ = handler; }

    [[nodiscard]] bool send_new_order(uint64_t order_id,
                                      bpt::messages::ExchangeId::Value exchange_id,
                                      uint64_t instrument_id,
                                      bpt::messages::OrderSide::Value side,
                                      bpt::messages::OrderType::Value order_type,
                                      bpt::messages::TimeInForce::Value tif,
                                      int64_t price,
                                      uint64_t quantity,
                                      uint8_t exec_inst,
                                      const std::string& exchange_symbol) override {
        using bpt::messages::NewOrder;
        using bpt::messages::OrderType;

        if (quantity == 0) {
            bpt::common::log::warn("[OrderGW] Rejected order_id={}: quantity is zero", order_id);
            return false;
        }
        if (order_type != OrderType::MARKET && price <= 0) {
            bpt::common::log::warn("[OrderGW] Rejected order_id={}: price={} invalid for non-MARKET order",
                                   order_id,
                                   price);
            return false;
        }
        if (exchange_symbol.empty()) {
            bpt::common::log::warn("[OrderGW] Rejected order_id={}: exchange_symbol is empty", order_id);
            return false;
        }

        order_pub_->template publish<NewOrder>([&](NewOrder& msg) {
            msg.orderId(order_id)
                .exchangeId(exchange_id)
                .instrumentId(instrument_id)
                .side(side)
                .orderType(order_type)
                .timeInForce(tif)
                .price(price)
                .quantity(quantity)
                .timestampNs(bpt::common::util::TscClock::now_epoch_ns())
                .execInst(exec_inst)
                .putExchangeSymbol(exchange_symbol);
        });
        return true;
    }

    void send_cancel(uint64_t order_id,
                     bpt::messages::ExchangeId::Value exchange_id,
                     uint64_t instrument_id) override {
        using bpt::messages::CancelOrder;
        order_pub_->template publish<CancelOrder>([&](CancelOrder& msg) {
            msg.orderId(order_id)
                .exchangeId(exchange_id)
                .instrumentId(instrument_id)
                .timestampNs(bpt::common::util::TscClock::now_epoch_ns());
        });
    }

    void send_cancel_all(bpt::messages::ExchangeId::Value exchange_id, uint64_t instrument_id) override {
        using bpt::messages::CancelAll;
        order_pub_->template publish<CancelAll>([&](CancelAll& msg) {
            msg.exchangeId(exchange_id)
                .instrumentId(instrument_id)
                .timestampNs(bpt::common::util::TscClock::now_epoch_ns());
        });
    }

    void send_modify(uint64_t order_id,
                     bpt::messages::ExchangeId::Value exchange_id,
                     uint64_t instrument_id,
                     int64_t new_price,
                     uint64_t new_quantity) override {
        using bpt::messages::ModifyOrder;
        order_pub_->template publish<ModifyOrder>([&](ModifyOrder& msg) {
            msg.orderId(order_id)
                .exchangeId(exchange_id)
                .instrumentId(instrument_id)
                .newPrice(new_price)
                .newQuantity(new_quantity)
                .timestampNs(bpt::common::util::TscClock::now_epoch_ns());
        });
    }

    void send_account_snapshot_request(bpt::messages::ExchangeId::Value exchange_id,
                                       uint64_t correlation_id) override {
        using bpt::messages::AccountSnapshotRequest;
        order_pub_->template publish<AccountSnapshotRequest>([&](AccountSnapshotRequest& msg) {
            msg.exchangeId(exchange_id)
                .correlationId(correlation_id)
                .timestampNs(bpt::common::util::TscClock::now_epoch_ns());
        });
    }

    int poll(int fragment_limit = 10) override {
        int total = 0;
        total += exec_report_sub_->poll(fragment_limit);
        total += heartbeat_sub_->poll(fragment_limit);
        if (account_snapshot_sub_)
            total += account_snapshot_sub_->poll(fragment_limit);
        return total;
    }

    [[nodiscard]] uint64_t last_heartbeat_ns() const override { return last_heartbeat_ns_; }

private:
    void handle_exec_report_fragment(aeron::AtomicBuffer& buf,
                                     aeron::util::index_t offset,
                                     aeron::util::index_t length,
                                     aeron::Header& /*hdr_aeron*/) {
        using bpt::messages::ExecutionReport;
        using bpt::messages::MessageHeader;

        if (static_cast<std::size_t>(length) < MessageHeader::encodedLength())
            return;

        char* data = reinterpret_cast<char*>(buf.buffer()) + offset;
        MessageHeader hdr(data, static_cast<std::size_t>(length));

        if (hdr.templateId() != ExecutionReport::sbeTemplateId())
            return;

        ExecutionReport msg;
        msg.wrapForDecode(data,
                          MessageHeader::encodedLength(),
                          hdr.blockLength(),
                          hdr.version(),
                          static_cast<std::size_t>(length));

        if (handler_ != nullptr) [[likely]]
            handler_->on_exec_report(msg);
    }

    void handle_heartbeat_fragment(aeron::AtomicBuffer& buf,
                                   aeron::util::index_t offset,
                                   aeron::util::index_t length,
                                   aeron::Header& /*hdr_aeron*/) {
        using bpt::messages::MessageHeader;
        using bpt::messages::OrderGatewayHeartbeat;

        if (static_cast<std::size_t>(length) < MessageHeader::encodedLength())
            return;

        char* data = reinterpret_cast<char*>(buf.buffer()) + offset;
        MessageHeader hdr(data, static_cast<std::size_t>(length));

        if (hdr.templateId() != OrderGatewayHeartbeat::sbeTemplateId())
            return;

        OrderGatewayHeartbeat msg;
        msg.wrapForDecode(data,
                          MessageHeader::encodedLength(),
                          hdr.blockLength(),
                          hdr.version(),
                          static_cast<std::size_t>(length));

        last_heartbeat_ns_ = msg.timestampNs();

        if (handler_ != nullptr) [[likely]]
            handler_->on_og_heartbeat(msg);
    }

    void handle_account_snapshot_fragment(aeron::AtomicBuffer& buf,
                                          aeron::util::index_t offset,
                                          aeron::util::index_t length,
                                          aeron::Header& /*hdr_aeron*/) {
        using bpt::messages::AccountSnapshot;
        using bpt::messages::MessageHeader;

        if (static_cast<std::size_t>(length) < MessageHeader::encodedLength())
            return;

        char* data = reinterpret_cast<char*>(buf.buffer()) + offset;
        MessageHeader hdr(data, static_cast<std::size_t>(length));

        if (hdr.templateId() != AccountSnapshot::sbeTemplateId())
            return;

        AccountSnapshot msg;
        msg.wrapForDecode(data,
                          MessageHeader::encodedLength(),
                          hdr.blockLength(),
                          hdr.version(),
                          static_cast<std::size_t>(length));

        if (handler_ != nullptr) [[likely]]
            handler_->on_account_snapshot(msg);
    }

    std::unique_ptr<bpt::common::aeron::Publisher> order_pub_;
    std::unique_ptr<bpt::common::aeron::Subscriber> exec_report_sub_;
    std::unique_ptr<bpt::common::aeron::Subscriber> heartbeat_sub_;
    std::unique_ptr<bpt::common::aeron::Subscriber> account_snapshot_sub_;
    Handler* handler_{nullptr};
    uint64_t last_heartbeat_ns_{0};
};

}  // namespace bpt::strategy::order

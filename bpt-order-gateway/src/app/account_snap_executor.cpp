#include "order_gateway/app/account_snap_executor.h"

#include <messages/ExchangeId.h>

#include <bpt_common/logging.h>
#include <bpt_common/util/tsc_clock.h>

namespace bpt::order_gateway::app {

AccountSnapExecutor::AccountSnapExecutor(messaging::api::AccountSnapshotPublisher& pub) : pub_(pub) {}

AccountSnapExecutor::~AccountSnapExecutor() {
    stop();
}

void AccountSnapExecutor::start() {
    stop_flag_.store(false, std::memory_order_release);
    thread_ = std::thread([this] { run(); });
}

void AccountSnapExecutor::stop() {
    if (!thread_.joinable())
        return;
    stop_flag_.store(true, std::memory_order_release);
    cv_.notify_all();
    thread_.join();
}

void AccountSnapExecutor::request_fetch(std::shared_ptr<adapter::IOrderAdapter> adapter, uint64_t correlation_id) {
    {
        std::lock_guard<std::mutex> g(mu_);
        queue_.push_back(Request{std::move(adapter), correlation_id});
    }
    cv_.notify_one();
}

void AccountSnapExecutor::run() {
    while (true) {
        Request req;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return stop_flag_.load(std::memory_order_acquire) || !queue_.empty(); });
            if (stop_flag_.load(std::memory_order_acquire) && queue_.empty())
                return;
            req = std::move(queue_.front());
            queue_.pop_front();
        }

        const auto exchange_id = req.adapter->exchange_id();
        try {
            auto snap = req.adapter->fetch_account_snapshot(req.correlation_id);
            pub_.publish(snap);
        } catch (const std::exception& e) {
            if (req.correlation_id == 0) {
                bpt::common::log::warn("Periodic AccountSnapshot fetch failed for {}: {}",
                                       bpt::messages::ExchangeId::c_str(exchange_id),
                                       e.what());
            } else {
                bpt::common::log::error("AccountSnapshot fetch failed for exchange={}: {}",
                                        bpt::messages::ExchangeId::c_str(exchange_id),
                                        e.what());
                adapter::AccountSnapshotData empty;
                empty.exchange_id = exchange_id;
                empty.correlation_id = req.correlation_id;
                empty.timestamp_ns = bpt::common::util::WallClock::now_ns();
                pub_.publish(empty);
            }
        }
    }
}

}  // namespace bpt::order_gateway::app

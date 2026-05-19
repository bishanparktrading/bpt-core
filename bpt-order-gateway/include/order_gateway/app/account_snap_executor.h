#pragma once

/// \file
/// \brief Single dedicated thread that runs blocking REST account-snapshot
///        fetches off the main poll loop.
///
/// Replaces the old `std::thread([..]).detach()` pattern. Main poll thread
/// pushes a `Request{adapter, correlation_id}` and returns immediately;
/// the executor thread drains the queue, calls
/// `adapter->fetch_account_snapshot(...)` (blocking HTTPS), and publishes
/// the result via `AccountSnapshotPublisher::publish(...)`.
///
/// Single-writer guarantees:
///   - account_snap_pub_ is touched only from the executor thread (Aeron
///     `Publication` is not safe for concurrent multi-writer use).
///   - Lifecycle: `start()` → `stop()` exactly once.

#include "order_gateway/adapter/common/i_order_adapter.h"
#include "order_gateway/messaging/publishers/api/account_snapshot_publisher.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace bpt::order_gateway::app {

class AccountSnapExecutor {
public:
    AccountSnapExecutor(messaging::api::AccountSnapshotPublisher& pub);

    AccountSnapExecutor(const AccountSnapExecutor&) = delete;
    AccountSnapExecutor& operator=(const AccountSnapExecutor&) = delete;

    ~AccountSnapExecutor();

    void start();
    void stop();

    /// \brief Enqueue a fetch + publish. Called from the main poll thread.
    ///        Non-blocking. `correlation_id == 0` is treated as a periodic
    ///        republish (failures log at warn, not error).
    void request_fetch(std::shared_ptr<adapter::IOrderAdapter> adapter, uint64_t correlation_id);

private:
    struct Request {
        std::shared_ptr<adapter::IOrderAdapter> adapter;
        uint64_t correlation_id{0};
    };

    void run();

    messaging::api::AccountSnapshotPublisher& pub_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Request> queue_;
    std::atomic<bool> stop_flag_{false};
    std::thread thread_;
};

}  // namespace bpt::order_gateway::app

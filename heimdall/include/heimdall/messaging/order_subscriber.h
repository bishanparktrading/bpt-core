#pragma once

#include <Aeron.h>
#include <FragmentAssembler.h>
#include <bifrost_protocol/AccountSnapshotRequest.h>
#include <bifrost_protocol/CancelAll.h>
#include <bifrost_protocol/CancelOrder.h>
#include <bifrost_protocol/ModifyOrder.h>
#include <bifrost_protocol/NewOrder.h>

#include <functional>
#include <memory>
#include <string>

namespace heimdall::messaging {

using OnNewOrderFn = std::function<void(const bifrost::protocol::NewOrder&)>;
using OnCancelFn = std::function<void(const bifrost::protocol::CancelOrder&)>;
using OnCancelAllFn = std::function<void(const bifrost::protocol::CancelAll&)>;
using OnModifyFn = std::function<void(const bifrost::protocol::ModifyOrder&)>;
using OnAccountSnapshotRequestFn =
    std::function<void(const bifrost::protocol::AccountSnapshotRequest&)>;

class OrderSubscriber {
public:
    OrderSubscriber(std::shared_ptr<aeron::Aeron> aeron, const std::string& channel, int stream_id);

    // Poll for incoming order messages. Returns number of fragments processed.
    int poll(int fragment_limit = 10);

    // Callbacks — set before calling poll().
    OnNewOrderFn on_new_order;
    OnCancelFn on_cancel;
    OnCancelAllFn on_cancel_all;
    OnModifyFn on_modify;
    OnAccountSnapshotRequestFn on_account_snapshot_request;

private:
    void handle_fragment(aeron::AtomicBuffer& buf, aeron::util::index_t offset,
                         aeron::util::index_t length, aeron::Header& hdr);

    std::shared_ptr<aeron::Subscription> subscription_;
    std::unique_ptr<aeron::FragmentAssembler> assembler_;
};

}  // namespace heimdall::messaging

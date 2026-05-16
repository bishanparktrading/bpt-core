#include "backtester/messaging/subscribers/aeron_backtest_ack_subscriber.h"

#include <concurrent/AtomicBuffer.h>

#include <chrono>
#include <cstddef>
#include <thread>

namespace bpt::backtester::messaging {

using namespace std::chrono_literals;

AeronBacktestAckSubscriber::AeronBacktestAckSubscriber(std::shared_ptr<aeron::Subscription> sub)
    : sub_(std::move(sub)) {}

bool AeronBacktestAckSubscriber::wait_for(uint64_t expected_seq, std::chrono::milliseconds timeout) {
    bool found = false;
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    auto handler = [&](const aeron::concurrent::AtomicBuffer& buf,
                       aeron::index_t offset,
                       aeron::index_t length,
                       const aeron::Header& /*hdr*/) {
        if (found)
            return;

        const std::byte* raw = reinterpret_cast<const std::byte*>(buf.buffer() + offset);
        std::span<const std::byte> bytes(raw, static_cast<std::size_t>(length));

        try {
            const auto ack = codec_.decode(bytes);
            if (ack.tick_seq_num == expected_seq)
                found = true;
        } catch (...) {
            // Wrong template id or truncated frame — skip silently;
            // BacktestAckSubscriber's contract is "match the right seq
            // or time out", not "log every malformed fragment".
        }
    };

    while (!found && std::chrono::steady_clock::now() < deadline) {
        int fragments = sub_->poll(handler, 10);
        if (fragments == 0)
            std::this_thread::sleep_for(10us);
    }

    return found;
}

}  // namespace bpt::backtester::messaging

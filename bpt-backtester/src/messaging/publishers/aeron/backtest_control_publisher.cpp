#include "backtester/messaging/publishers/aeron/backtest_control_publisher.h"

#include <concurrent/AtomicBuffer.h>

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <thread>

namespace bpt::backtester::messaging::aeron {

using namespace std::chrono_literals;

BacktestControlPublisher::BacktestControlPublisher(std::shared_ptr<::aeron::Publication> pub) : pub_(std::move(pub)) {}

void BacktestControlPublisher::send(bpt::messages::BacktestCommand::Value cmd,
                                    uint64_t tick_seq_num,
                                    uint64_t simulation_ts) {
    alignas(8) std::byte scratch[SbeBacktestControlCodec::kRecommendedScratchSize];
    const auto bytes = codec_.encode(BacktestControlMsg{cmd, tick_seq_num, simulation_ts}, scratch);

    ::aeron::concurrent::AtomicBuffer ab(reinterpret_cast<uint8_t*>(scratch), bytes.size());
    const auto len = static_cast<::aeron::index_t>(bytes.size());

    // Spin on back-pressure for up to 30 seconds.
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (true) {
        int64_t result = pub_->offer(ab, 0, len);
        if (result > 0)
            return;

        if (result == ::aeron::BACK_PRESSURED || result == ::aeron::NOT_CONNECTED || result == ::aeron::ADMIN_ACTION) {
            if (std::chrono::steady_clock::now() > deadline)
                throw std::runtime_error(
                    "[aeron::BacktestControlPublisher] offer timed out (back-pressure/not-connected)");
            std::this_thread::sleep_for(100us);
            continue;
        }

        throw std::runtime_error("[aeron::BacktestControlPublisher] offer failed: " + std::to_string(result));
    }
}

}  // namespace bpt::backtester::messaging::aeron

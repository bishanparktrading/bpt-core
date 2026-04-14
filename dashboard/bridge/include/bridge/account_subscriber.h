#pragma once

#include <Aeron.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace bridge {

// Subscribes to Heimdall's AccountSnapshot stream (default 3004) and
// delivers decoded balance / equity values from the live exchange account.
//
// The dashboard uses these as the canonical equity baseline so the equity
// curve reflects the actual exchange balance rather than a static
// `starting_capital` config value.
class AccountSubscriber {
public:
    struct Snapshot {
        uint64_t ts_ns;
        uint8_t  exchange_id;
        double   available_balance;  // natural units (e.g. USDC)
        double   total_equity;       // natural units; falls back to balance when 0
    };

    using Handler = std::function<void(const Snapshot&)>;

    AccountSubscriber(std::shared_ptr<aeron::Aeron> aeron,
                      const std::string& channel,
                      int32_t stream_id);

    void set_handler(Handler h) { handler_ = std::move(h); }

    int poll(int fragment_limit = 8);

private:
    void on_fragment(const aeron::concurrent::AtomicBuffer& buffer,
                     aeron::util::index_t offset,
                     aeron::util::index_t length,
                     const aeron::Header& header);

    std::shared_ptr<aeron::Subscription> sub_;
    Handler handler_;
};

}  // namespace bridge

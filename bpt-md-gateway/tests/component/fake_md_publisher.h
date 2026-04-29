#pragma once

#include "md_gateway/md/md_types.h"

#include <cstdint>
#include <optional>

namespace bpt::md_gateway::test {

// Concrete test publisher — no virtual interface. Decoders/adapters are
// templated on the publisher type so the test instantiates them with
// FakeMdPublisher directly (same shape the prod path uses with MdPublisher).
class FakeMdPublisher {
public:
    void publish(const md::MdBbo& bbo) {
        last_bbo = bbo;
        ++bbo_count;
    }

    void publish(const md::MdTrade& trade) {
        last_trade = trade;
        ++trade_count;
    }

    void publish(const md::MdOrderBook& book) {
        last_order_book = book;
        ++order_book_count;
    }

    uint64_t drop_count() const { return 0; }

    void reset() {
        last_bbo.reset();
        last_trade.reset();
        last_order_book.reset();
        bbo_count = 0;
        trade_count = 0;
        order_book_count = 0;
    }

    std::optional<md::MdBbo> last_bbo;
    std::optional<md::MdTrade> last_trade;
    std::optional<md::MdOrderBook> last_order_book;
    int bbo_count{0};
    int trade_count{0};
    int order_book_count{0};
};

}  // namespace bpt::md_gateway::test

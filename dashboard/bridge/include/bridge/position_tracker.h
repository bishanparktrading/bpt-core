#pragma once

#include "bridge/message_encoder.h"

#include <cstdint>

namespace bridge {

// Running position state derived from a stream of fills.
// Responsible for computing realised PnL and equity — the bridge is the
// authoritative source of these values, the frontend just displays them.
class PositionTracker {
public:
    explicit PositionTracker(double starting_capital) : starting_capital_(starting_capital), equity_(starting_capital) {}

    struct FillResult {
        double realized_pnl;   // realised on this fill (0 unless closing/reducing)
        double equity;         // equity after this fill
        double net_qty;
        double avg_entry;
    };

    FillResult apply(encode::Side side, double qty, double price);

    double net_qty()       const noexcept { return net_qty_;  }
    double avg_entry()     const noexcept { return avg_entry_; }
    double equity()        const noexcept { return equity_;   }
    double starting_capital() const noexcept { return starting_capital_; }

    // Mark-to-market PnL given the current market price.
    double unrealized_pnl(double mark_price) const noexcept {
        if (net_qty_ == 0.0) return 0.0;
        return (mark_price - avg_entry_) * net_qty_;
    }

private:
    double starting_capital_;
    double equity_;
    double net_qty_{0.0};
    double avg_entry_{0.0};
};

}  // namespace bridge

#pragma once

/// \file
/// \brief Inbound port: MdSubscribeBatch control-plane source.

#include <functional>

namespace bpt::messages {
class MdSubscribeBatch;
}

namespace bpt::md_gateway::messaging {

class IMdControlSource {
public:
    /// Handler is invoked with each decoded MdSubscribeBatch fragment.
    /// The reference is only valid for the duration of the call.
    using BatchHandler = std::function<void(bpt::messages::MdSubscribeBatch&)>;

    virtual ~IMdControlSource() = default;

    /// Drain available control fragments, dispatching each to `handler`.
    /// \return Number of fragments processed; 0 means idle.
    virtual int poll(BatchHandler handler) = 0;
};

}  // namespace bpt::md_gateway::messaging

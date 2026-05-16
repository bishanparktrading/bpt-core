#pragma once

/// \file
/// \brief Outbound port: fee schedule publish.

namespace bpt::refdata::refdata {
struct FeeScheduleState;
}

namespace bpt::refdata::messaging::api {

class FeeSchedulePublisher {
public:
    virtual ~FeeSchedulePublisher() = default;

    virtual void publish(const refdata::FeeScheduleState& fs) = 0;
};

}  // namespace bpt::refdata::messaging::api

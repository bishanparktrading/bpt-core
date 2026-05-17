#pragma once

/// @file
/// Port: exec-report subscriber. Decodes SBE `ExecutionReport` once and
/// dispatches via the `on_report` std::function callback. The Aeron
/// concrete in `aeron/exec_report_subscriber.h` is the prod
/// implementation; a sim variant could feed reports directly from a
/// tape replay or test driver.

#include <messages/ExecutionReport.h>

#include <functional>

namespace bpt::analytics::messaging::api {

class ExecReportSubscriber {
public:
    using OnReportFn = std::function<void(const bpt::messages::ExecutionReport&)>;

    virtual ~ExecReportSubscriber() = default;

    virtual int poll(int fragment_limit = 10) = 0;

    OnReportFn on_report;
};

}  // namespace bpt::analytics::messaging::api

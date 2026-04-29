#pragma once

#include "md_gateway/messaging/i_md_control_source.h"

#include <Aeron.h>

#include <memory>
#include <string>
#include <bpt_common/aeron/subscriber.h>

namespace bpt::md_gateway::messaging {

class MdControlSubscriber final : public IMdControlSource {
public:
    MdControlSubscriber(std::shared_ptr<::aeron::Aeron> aeron,
                        const std::string& channel,
                        int stream_id);

    int poll(BatchHandler handler) override;

private:
    std::unique_ptr<bpt::common::aeron::Subscriber> subscription_;
    BatchHandler current_handler_;
};

}  // namespace bpt::md_gateway::messaging

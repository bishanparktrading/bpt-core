#pragma once

#include "huginn/md/md_types.h"

namespace huginn::messaging {

class IMdPublisher {
public:
    virtual ~IMdPublisher() = default;

    virtual void publish(const md::MdBbo& bbo) = 0;
    virtual void publish(const md::MdTrade& trade) = 0;
    virtual void publish(const md::MdOrderBook& book) = 0;
};

}  // namespace huginn::messaging

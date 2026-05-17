#pragma once

/// \file
/// Port: VolSurface subscriber. Aeron concrete in
/// `aeron/vol_surface_subscriber.h`.

#include <messages/VolSurface.h>

#include <functional>

namespace bpt::radar::messaging::api {

class VolSurfaceSubscriber {
public:
    using OnVolSurfaceFn = std::function<void(bpt::messages::VolSurface&)>;

    virtual ~VolSurfaceSubscriber() = default;

    virtual int poll(int fragment_limit = 4) = 0;

    OnVolSurfaceFn on_vol_surface;
};

}  // namespace bpt::radar::messaging::api

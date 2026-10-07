#include "camera/daheng_backend.hpp"

#include <utility>

namespace vsort::service {

DahengBackend::DahengBackend(camera::DahengOptions options)
    : options_{options}
    , discovery_{camera::makeDahengDiscovery()} {}

Result<std::vector<camera::DiscoveredCamera>> DahengBackend::discover() {
    return discovery_->discover();
}

std::unique_ptr<camera::ICamera> DahengBackend::create() {
    return camera::makeDahengCamera(options_);
}

} // namespace vsort::service

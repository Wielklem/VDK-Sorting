#pragma once

#include <memory>

#include <vsort/camera/discovery.hpp>

namespace vsort::camera {

// Daheng Galaxy adapter (M30.10). USB3 and GigE.
// The Galaxy SDK is not visible in this header.
[[nodiscard]] std::unique_ptr<ICameraDiscovery> makeDahengDiscovery();

} // namespace vsort::camera

#pragma once

#include <cstddef>
#include <memory>

#include <vsort/camera/camera.hpp>
#include <vsort/camera/discovery.hpp>

namespace vsort::camera {

struct DahengOptions {
    // Preallocated frame buffers per camera (P20.20). When the consumer holds more frames than
    // this, new frames are dropped until one is released.
    std::size_t poolFrames{16};
};

// Daheng Galaxy adapter (M30.10). USB3 and GigE; open by serial number.
// The Galaxy SDK is not visible in this header.
[[nodiscard]] std::unique_ptr<ICamera> makeDahengCamera(const DahengOptions& options = {});
[[nodiscard]] std::unique_ptr<ICameraDiscovery> makeDahengDiscovery();

} // namespace vsort::camera

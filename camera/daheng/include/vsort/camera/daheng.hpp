#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <vsort/camera/camera.hpp>
#include <vsort/camera/discovery.hpp>
#include <vsort/camera/ip_config.hpp>

namespace vsort::camera {

struct DahengOptions {
    // Preallocated frame buffers per camera (P20.20). When the consumer holds more frames than
    // this, new frames are dropped until one is released.
    std::size_t poolFrames{16};

    // GigE only (P20.40). Failures are logged as warnings; the camera still opens.
    bool optimalPacketSize{true};        // largest packet size the NIC path allows (jumbo)
    std::int64_t packetDelay{0};         // GevSCPD, 0 = leave as is
    std::uint64_t acquisitionBuffers{0}; // SDK-side buffers, 0 = SDK default
};

// Daheng Galaxy adapter (M30.10). USB3 and GigE; open by serial number.
// The Galaxy SDK is not visible in this header.
[[nodiscard]] std::unique_ptr<ICamera> makeDahengCamera(const DahengOptions& options = {});
[[nodiscard]] std::unique_ptr<ICameraDiscovery> makeDahengDiscovery();
[[nodiscard]] std::unique_ptr<IGigEConfigurator> makeDahengGigEConfigurator();

} // namespace vsort::camera

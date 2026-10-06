#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vsort/common/error.hpp>

namespace vsort::camera {

enum class Transport : std::uint8_t { Usb3 = 1, GigE };

// One camera found on the bus. The network fields are empty for USB3.
struct DiscoveredCamera {
    std::string serial;
    std::string model;
    Transport transport{Transport::Usb3};
    std::string mac;
    std::string ip;
    std::string subnetMask;
    std::string gateway;
};

// Implemented by the Daheng adapter (M30.10).
class ICameraDiscovery {
public:
    ICameraDiscovery() = default;
    virtual ~ICameraDiscovery() = default;
    ICameraDiscovery(const ICameraDiscovery&) = delete;
    ICameraDiscovery& operator=(const ICameraDiscovery&) = delete;
    ICameraDiscovery(ICameraDiscovery&&) = delete;
    ICameraDiscovery& operator=(ICameraDiscovery&&) = delete;

    // Blocking scan of all transports. An empty list is not an error.
    [[nodiscard]] virtual Result<std::vector<DiscoveredCamera>> discover() = 0;
};

} // namespace vsort::camera

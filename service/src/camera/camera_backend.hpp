#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/camera/camera_map.hpp>
#include <vsort/camera/discovery.hpp>
#include <vsort/common/error.hpp>

namespace vsort::service {

// Where the cameras come from: real hardware (Daheng) or a recorded session (replay).
class ICameraBackend {
public:
    ICameraBackend() = default;
    virtual ~ICameraBackend() = default;
    ICameraBackend(const ICameraBackend&) = delete;
    ICameraBackend& operator=(const ICameraBackend&) = delete;
    ICameraBackend(ICameraBackend&&) = delete;
    ICameraBackend& operator=(ICameraBackend&&) = delete;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // Blocking scan, called from the camera supervisor thread only. An empty list is not an error.
    [[nodiscard]] virtual Result<std::vector<camera::DiscoveredCamera>> discover() = 0;

    // A fresh, closed camera. Called once per (re)connect attempt, from any thread.
    [[nodiscard]] virtual std::unique_ptr<camera::ICamera> create() = 0;

    // true: camera_map and camera_settings are read from and saved to the config store.
    // false (replay): fixedMap() is used and the config store is not touched.
    [[nodiscard]] virtual bool persistent() const noexcept { return true; }
    [[nodiscard]] virtual std::vector<camera::CameraMapEntry> fixedMap() const { return {}; }
};

} // namespace vsort::service

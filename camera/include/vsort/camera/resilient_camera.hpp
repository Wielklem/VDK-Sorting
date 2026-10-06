#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include <vsort/camera/camera.hpp>
#include <vsort/camera/health.hpp>

namespace vsort::camera {

struct ResilientOptions {
    std::chrono::milliseconds retryInitial{200}; // first delay between reconnect attempts
    std::chrono::milliseconds retryMax{5000};    // delay doubles up to this
};

class IMonitoredCamera : public ICamera {
public:
    // Lock-free snapshot, safe from any thread.
    [[nodiscard]] virtual CameraHealth health() const = 0;
};

// Creates a fresh, closed camera. Called once per (re)connect attempt.
using CameraFactory = std::function<std::unique_ptr<ICamera>()>;

// Wraps any ICamera (Daheng, replay). After open() succeeds it:
//  - counts received/dropped frames and frame-ID gaps (P20.50),
//  - on an Offline event closes the camera and retries open() with backoff, then restores the
//    last applied settings, the frame callback and streaming.
// isOpen() stays true while reconnecting. While reconnecting, info(), settings() and
// applySettings() fail with DeviceError; start() is remembered and runs after the reconnect.
// Frame IDs restart after a reconnect; that counts as no gap.
[[nodiscard]] std::unique_ptr<IMonitoredCamera>
makeResilientCamera(CameraFactory factory, const ResilientOptions& options = {});

} // namespace vsort::camera

#pragma once

#include <cstdint>
#include <vector>

namespace vsort::service {

// P30.86: frame rates per camera, put on the bus by the analysis module about once per second and
// sent to the HMI as CameraRatesEvent (MSG-30-03).
struct CameraRate {
    std::uint16_t cameraId{0};
    double incomingFps{0.0}; // frames the camera delivered to the analysis
    double analysedFps{0.0}; // frames analysed without error (every sensor of that camera)
};

// Shortcut: only cameras with a sensor (an analysis worker) are listed, and nothing is sent while
// the analysis is disabled; the HMI then shows no rate.
struct CameraRates {
    std::vector<CameraRate> cameras;
};

} // namespace vsort::service

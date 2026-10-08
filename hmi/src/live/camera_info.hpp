#pragma once

#include <QString>
#include <cstdint>

namespace vsort::hmi {

enum class CameraLinkState : std::uint8_t { Closed = 0, Open, Streaming, Reconnecting };

// What the HMI knows about one camera (from CameraEntry, MSG 3011/3012).
struct CameraInfo {
    std::uint16_t id{0};
    QString serial;
    QString model;
    CameraLinkState state{CameraLinkState::Closed};
    bool previewEnabled{false};
    QString shmName; // empty until the service wrote the first preview frame
    std::uint32_t generation{0};
};

// Camera settings as the service reports them (MSG 3013/3014). The HMI edits exposure and gain
// only; the other fields are sent back unchanged.
struct CameraSettingsData {
    double exposureUs{0.0};
    double gainDb{0.0};
    std::uint32_t roiX{0};
    std::uint32_t roiY{0};
    std::uint32_t roiWidth{0};   // 0 = full sensor
    std::uint32_t roiHeight{0};  // 0 = full sensor
    std::uint8_t triggerMode{3}; // 1 free run, 2 software, 3 hardware
    bool triggerRising{true};
};

// P30.86: frame rates measured in the service (CameraRatesEvent, MSG-30-03).
struct CameraRateData {
    std::uint16_t cameraId{0};
    double incomingFps{0.0}; // frames the camera delivered to the analysis
    double analysedFps{0.0}; // frames analysed without error
};

} // namespace vsort::hmi

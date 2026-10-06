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

} // namespace vsort::hmi

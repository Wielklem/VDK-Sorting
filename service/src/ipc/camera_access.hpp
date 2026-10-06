#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/common/error.hpp>

namespace vsort::service {

enum class CameraRuntimeState : std::uint8_t { Closed = 0, Open, Streaming, Reconnecting };

struct CameraListEntry {
    std::uint16_t id{0}; // logical camera ID (P20.80)
    std::string serial;
    std::string model;
    CameraRuntimeState state{CameraRuntimeState::Closed};
};

// What the IPC command handler needs from the camera side. Implemented by the camera module
// once it is wired into the service; until then NullCameraAccess is used.
class ICameraAccess {
public:
    ICameraAccess() = default;
    virtual ~ICameraAccess() = default;
    ICameraAccess(const ICameraAccess&) = delete;
    ICameraAccess& operator=(const ICameraAccess&) = delete;
    ICameraAccess(ICameraAccess&&) = delete;
    ICameraAccess& operator=(ICameraAccess&&) = delete;

    // Must be thread-safe: called from the IPC thread.
    [[nodiscard]] virtual std::vector<CameraListEntry> list() const = 0;
    // NotFound for an unknown id.
    [[nodiscard]] virtual Result<camera::CameraSettings> settings(std::uint16_t id) const = 0;
    // NotFound for an unknown id; InvalidArgument / NotSupported from the camera.
    [[nodiscard]] virtual Result<> apply(std::uint16_t id,
                                         const camera::CameraSettings& settings) = 0;
};

class NullCameraAccess final : public ICameraAccess {
public:
    [[nodiscard]] std::vector<CameraListEntry> list() const override { return {}; }
    [[nodiscard]] Result<camera::CameraSettings> settings(std::uint16_t /*id*/) const override {
        return makeError(Errc::NotFound, "no cameras");
    }
    [[nodiscard]] Result<> apply(std::uint16_t /*id*/,
                                 const camera::CameraSettings& /*settings*/) override {
        return makeError(Errc::NotFound, "no cameras");
    }
};

} // namespace vsort::service

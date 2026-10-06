#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <vsort/common/error.hpp>
#include <vsort/common/timestamp.hpp>
#include <vsort/common/types.hpp>

namespace vsort::camera {

enum class PixelFormat : std::uint8_t { Mono8 = 1, BayerRG8, Rgb8, Bgr8 };

// V1 uses Hardware (external PLC trigger). FreeRun and Software are for setup and tests.
enum class TriggerMode : std::uint8_t { FreeRun = 1, Software, Hardware };

enum class TriggerEdge : std::uint8_t { Rising = 1, Falling };

// Pixels, relative to the full sensor.
struct Roi {
    std::uint32_t x{0};
    std::uint32_t y{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

struct CameraInfo {
    std::string serial;
    std::string model;
    std::uint32_t sensorWidth{0};
    std::uint32_t sensorHeight{0};
};

struct CameraSettings {
    double exposureUs{10000.0};
    double gainDb{0.0};
    Roi roi; // width/height 0 = full sensor
    TriggerMode triggerMode{TriggerMode::Hardware};
    TriggerEdge triggerEdge{TriggerEdge::Rising};
};

struct FrameMetadata {
    FrameId frameId;                    // from the camera; +1 per hardware trigger
    Timestamp hostTimestamp;            // arrival on the PC (monotonic)
    std::uint64_t deviceTimestampNs{0}; // camera clock, 0 if unavailable
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t strideBytes{0};
    PixelFormat pixelFormat{PixelFormat::Mono8};
    std::uint16_t cameraIndex{0}; // logical camera ID (P20.80)
};

// Pixel data is read-only. `owner` keeps the buffer alive (ref-counted, pool-friendly, P20.20).
// Copy `owner` to keep the frame after the callback returns.
struct Frame {
    FrameMetadata meta;
    std::span<const std::byte> data;
    std::shared_ptr<const void> owner;
};

// Called from the camera's grab thread. Must be fast and must not throw.
using FrameCallback = std::function<void(const Frame&)>;

// Implemented by the Daheng adapter (M30.10) and the replay camera (M30.20).
// Lifecycle: open() -> applySettings() -> start() -> stop() -> close().
class ICamera {
public:
    ICamera() = default;
    virtual ~ICamera() = default;
    ICamera(const ICamera&) = delete;
    ICamera& operator=(const ICamera&) = delete;
    ICamera(ICamera&&) = delete;
    ICamera& operator=(ICamera&&) = delete;

    // Open by serial number.
    [[nodiscard]] virtual Result<> open(std::string_view serial) = 0;

    // Stops streaming if needed. Safe when not open. Must not throw.
    virtual void close() noexcept = 0;

    [[nodiscard]] virtual bool isOpen() const noexcept = 0;

    // Requires open.
    [[nodiscard]] virtual Result<CameraInfo> info() const = 0;
    [[nodiscard]] virtual Result<CameraSettings> settings() const = 0;

    // Requires open. Rejects invalid values (InvalidArgument) and unsupported modes (NotSupported).
    [[nodiscard]] virtual Result<> applySettings(const CameraSettings& settings) = 0;

    // Set before start(). Replaced, not appended.
    virtual void setFrameCallback(FrameCallback callback) = 0;

    // Requires open. Starts grabbing and calling the frame callback.
    [[nodiscard]] virtual Result<> start() = 0;

    // Stops grabbing. After return, no more callbacks. Safe when not streaming. Must not throw.
    virtual void stop() noexcept = 0;

    [[nodiscard]] virtual bool isStreaming() const noexcept = 0;
};

} // namespace vsort::camera

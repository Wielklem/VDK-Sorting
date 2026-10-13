#pragma once

#include <cstdint>
#include <optional>

#include <opencv2/core.hpp>

#include "capture/capture_types.hpp"

namespace vsort::service {

// One photo of one sensor, ready to be written.
struct CaptureImage {
    std::uint16_t sensorId{0};
    std::uint16_t cameraId{0};
    std::uint32_t seq{0}; // from reserve()
    std::uint64_t frameId{0};
    CaptureValues values;
    cv::Mat photo;   // lane ROI plus detection buffer, BGR, without drawings
    cv::Mat overlay; // the same with mask, objects and sizes drawn
};

// The analysis offers its results here (P60.100). Called on the analysis worker threads, so all
// methods are thread-safe and cheap. Usage per sensor and frame:
//   reserve() -> if a number came back: build the images, then commit() (or cancel() on a
//   failure). Nothing but reserve() runs when no capture wants the photo.
class ICaptureSink {
public:
    ICaptureSink() = default;
    virtual ~ICaptureSink() = default;
    ICaptureSink(const ICaptureSink&) = delete;
    ICaptureSink& operator=(const ICaptureSink&) = delete;
    ICaptureSink(ICaptureSink&&) = delete;
    ICaptureSink& operator=(ICaptureSink&&) = delete;

    // The photo number (1, 2, ...) when a running capture wants this photo: the sensor is
    // selected, its quota is not full and the filter accepts the values. nullopt otherwise.
    [[nodiscard]] virtual std::optional<std::uint32_t>
    reserve(std::uint16_t sensorId, const CaptureValues& values) noexcept = 0;
    // Hands over the images of a reserved photo. Never blocks; the disk write is asynchronous.
    virtual void commit(CaptureImage image) noexcept = 0;
    // Gives a reserved photo back (the images could not be made).
    virtual void cancel(std::uint16_t sensorId) noexcept = 0;
};

} // namespace vsort::service

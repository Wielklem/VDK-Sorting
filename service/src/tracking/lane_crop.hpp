#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include <nlohmann/json.hpp>

#include <vsort/camera/camera.hpp>
#include <vsort/common/error.hpp>

namespace vsort::service {

// P40.20: lane ROI -> pixel crop of a frame.

// Fractions (0..1) of the image, as stored in the "rois" config.
struct NormalizedRect {
    double x{0.0};
    double y{0.0};
    double width{1.0};
    double height{1.0};
};

// Looks up ROI `roiId` of `cameraId` in the "rois" config. roiId 0 = whole image.
// nullopt: not found.
[[nodiscard]] std::optional<NormalizedRect> findRoi(const nlohmann::json& roiConfig,
                                                    std::uint16_t cameraId, std::uint32_t roiId);

// 2 for Bayer (the crop must keep the colour pattern phase), else 1.
[[nodiscard]] std::uint32_t cropAlignment(camera::PixelFormat format) noexcept;
[[nodiscard]] std::uint32_t bytesPerPixel(camera::PixelFormat format) noexcept;

// Fractions -> pixels, rounded to the nearest pixel, clamped to the image, x/y/width/height
// multiples of `alignment` where the image allows it, at least `alignment` px (or the image)
// in size. Zero image size gives an empty Roi.
[[nodiscard]] camera::Roi toPixels(const NormalizedRect& rect, std::uint32_t imageWidth,
                                   std::uint32_t imageHeight, std::uint32_t alignment) noexcept;

// Zero-copy view of one lane in a frame. Keep `owner` to use it after the frame callback.
struct LaneImage {
    std::span<const std::byte> data; // starts at the crop's first pixel
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t strideBytes{0}; // the frame's stride
    camera::PixelFormat pixelFormat{camera::PixelFormat::Mono8};
    std::shared_ptr<const void> owner;
};

// InvalidArgument: crop empty or outside the frame, or buffer shorter than the metadata says.
[[nodiscard]] Result<LaneImage> cropFrame(const camera::Frame& frame, const camera::Roi& crop);

} // namespace vsort::service

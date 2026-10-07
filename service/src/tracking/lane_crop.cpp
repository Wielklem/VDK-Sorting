#include "tracking/lane_crop.hpp"

#include <algorithm>
#include <cmath>

namespace vsort::service {

using nlohmann::json;

std::optional<NormalizedRect> findRoi(const json& roiConfig, std::uint16_t cameraId,
                                      std::uint32_t roiId) {
    if (roiId == 0) {
        return NormalizedRect{};
    }
    if (!roiConfig.contains("cameras") || !roiConfig.at("cameras").is_array()) {
        return std::nullopt;
    }
    for (const auto& cam : roiConfig.at("cameras")) {
        if (cam.value("camera_id", -1) != cameraId) {
            continue;
        }
        for (const auto& roi : cam.value("rois", json::array())) {
            if (roi.value("id", std::uint32_t{0}) == roiId) {
                return NormalizedRect{.x = roi.value("x", 0.0),
                                      .y = roi.value("y", 0.0),
                                      .width = roi.value("width", 1.0),
                                      .height = roi.value("height", 1.0)};
            }
        }
    }
    return std::nullopt;
}

std::uint32_t cropAlignment(camera::PixelFormat format) noexcept {
    return format == camera::PixelFormat::BayerRG8 ? 2U : 1U;
}

std::uint32_t bytesPerPixel(camera::PixelFormat format) noexcept {
    return (format == camera::PixelFormat::Rgb8 || format == camera::PixelFormat::Bgr8) ? 3U : 1U;
}

namespace {

std::uint32_t toPixel(double fraction, std::uint32_t size) noexcept {
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    return static_cast<std::uint32_t>(std::lround(clamped * static_cast<double>(size)));
}

std::uint32_t alignDown(std::uint32_t value, std::uint32_t alignment) noexcept {
    return value - (value % alignment);
}

// One axis: start and length in pixels, both aligned, inside [0, size).
void axis(double start, double length, std::uint32_t size, std::uint32_t alignment,
          std::uint32_t& outStart, std::uint32_t& outLength) noexcept {
    const std::uint32_t usable = std::max(alignDown(size, alignment), std::min(size, alignment));
    std::uint32_t begin = alignDown(toPixel(start, size), alignment);
    std::uint32_t end = std::max(toPixel(start + length, size), begin);
    end = std::min(end - (end - begin) % alignment, usable);
    const std::uint32_t minLength = std::min(alignment, usable);
    if (end < begin + minLength) {
        begin = std::min(begin, usable - minLength);
        end = begin + minLength;
    }
    outStart = begin;
    outLength = end - begin;
}

} // namespace

camera::Roi toPixels(const NormalizedRect& rect, std::uint32_t imageWidth,
                     std::uint32_t imageHeight, std::uint32_t alignment) noexcept {
    if (imageWidth == 0 || imageHeight == 0) {
        return {};
    }
    const std::uint32_t a = std::max(alignment, 1U);
    camera::Roi out;
    axis(rect.x, rect.width, imageWidth, a, out.x, out.width);
    axis(rect.y, rect.height, imageHeight, a, out.y, out.height);
    return out;
}

Result<LaneImage> cropFrame(const camera::Frame& frame, const camera::Roi& crop) {
    const auto& meta = frame.meta;
    if (crop.width == 0 || crop.height == 0) {
        return makeError(Errc::InvalidArgument, "empty crop");
    }
    if (std::uint64_t{crop.x} + crop.width > meta.width ||
        std::uint64_t{crop.y} + crop.height > meta.height) {
        return makeError(Errc::InvalidArgument, "crop outside the frame");
    }
    const std::uint64_t bpp = bytesPerPixel(meta.pixelFormat);
    const std::uint64_t rowBytes = std::uint64_t{crop.width} * bpp;
    const std::uint64_t first = std::uint64_t{crop.y} * meta.strideBytes + crop.x * bpp;
    const std::uint64_t size = (std::uint64_t{crop.height} - 1U) * meta.strideBytes + rowBytes;
    if (std::uint64_t{meta.width} * bpp > meta.strideBytes || first + size > frame.data.size()) {
        return makeError(Errc::InvalidArgument, "frame buffer shorter than its metadata says");
    }
    return LaneImage{
        .data = frame.data.subspan(static_cast<std::size_t>(first), static_cast<std::size_t>(size)),
        .width = crop.width,
        .height = crop.height,
        .strideBytes = meta.strideBytes,
        .pixelFormat = meta.pixelFormat,
        .owner = frame.owner};
}

} // namespace vsort::service

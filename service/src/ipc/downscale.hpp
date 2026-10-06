#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/common/error.hpp>
#include <vsort/ipc/preview_ring.hpp>

namespace vsort::service {

struct ScaledImage {
    std::uint32_t width{0};
    std::uint32_t height{0};
    ipc::PreviewPixelFormat format{ipc::PreviewPixelFormat::Mono8};
    std::vector<std::byte> pixels; // tightly packed: stride = width * bytesPerPixel(format)
};

struct PreviewSize {
    std::uint32_t width{0};
    std::uint32_t height{0};
};

// Fits (srcWidth x srcHeight) into maxWidth, keeping the aspect ratio. Never upscales.
// Returns {0,0} for zero input.
[[nodiscard]] PreviewSize previewSize(std::uint32_t srcWidth, std::uint32_t srcHeight,
                                      std::uint32_t maxWidth) noexcept;

// Downscales a camera frame for the preview. Mono8 stays Mono8; Bayer, RGB and BGR become RGB8.
// InvalidArgument: empty frame, stride too small, buffer too short, maxWidth 0.
[[nodiscard]] Result<ScaledImage> downscaleForPreview(const camera::Frame& frame,
                                                      std::uint32_t maxWidth);

} // namespace vsort::service

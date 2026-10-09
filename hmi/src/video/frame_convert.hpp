#pragma once

#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <array>
#include <cstddef>
#include <vector>

#include <vsort/ipc/preview_ring.hpp>

namespace vsort::hmi {

// Mono8 -> Grayscale8, Rgb8 -> RGB888. Copies row by row (honours stride).
// Returns a null image when the frame is inconsistent (size, stride or pixel count).
[[nodiscard]] QImage toImage(const ipc::PreviewFrame& frame);

// 8-bit RGB -> HSV with the same integer steps as OpenCV COLOR_RGB2HSV: H 0..179, S and V 0..255.
[[nodiscard]] std::array<int, 3> rgbToHsv(int r, int g, int b) noexcept;

// Inclusive HSV range (OpenCV scale), as in the "analysis" config (P60.45).
struct HsvRange {
    std::array<int, 3> lower{0, 0, 0};
    std::array<int, 3> upper{179, 255, 255};
};

// HSV editor, one channel (0 = H, 1 = S, 2 = V) of `image` (any format) as RGB888: the channel
// value as grey (H scaled from 0..179 to 0..255), the pixels inside that channel's range blended
// 50 % green. `histogram` gets the pixel count per channel value (180 bins for H, 256 for S and V).
[[nodiscard]] QImage channelMask(const QImage& image, int channel, const HsvRange& range,
                                 std::vector<int>& histogram);

// HSV editor, result: `image` as RGB888 with the pixels inside all three ranges blended 50 %
// green (what the segmentation stage keeps before erode/dilate). `inRange` gets their count.
[[nodiscard]] QImage rangeOverlay(const QImage& image, const HsvRange& range, std::size_t& inRange);

// Largest rect with the aspect ratio of `content` that fits centred in `bounds`.
// Null rect when either size is empty.
[[nodiscard]] QRectF fitRect(QSizeF content, QSizeF bounds);

} // namespace vsort::hmi

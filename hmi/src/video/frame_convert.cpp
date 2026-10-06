#include "video/frame_convert.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace vsort::hmi {

namespace {
constexpr std::uint32_t kMaxDimension = 16384U;
} // namespace

QImage toImage(const ipc::PreviewFrame& frame) {
    const auto& info = frame.info;
    if (info.width == 0U || info.height == 0U || info.width > kMaxDimension ||
        info.height > kMaxDimension) {
        return {};
    }
    const std::size_t rowBytes = std::size_t{info.width} * ipc::bytesPerPixel(info.pixelFormat);
    if (info.strideBytes < rowBytes) {
        return {};
    }
    const std::size_t needed = std::size_t{info.strideBytes} * (info.height - 1U) + rowBytes;
    if (frame.pixels.size() < needed) {
        return {};
    }

    const auto format = info.pixelFormat == ipc::PreviewPixelFormat::Rgb8
                            ? QImage::Format_RGB888
                            : QImage::Format_Grayscale8;
    QImage image{static_cast<int>(info.width), static_cast<int>(info.height), format};
    if (image.isNull()) {
        return {};
    }
    for (std::uint32_t y = 0; y < info.height; ++y) {
        std::memcpy(image.scanLine(static_cast<int>(y)),
                    frame.pixels.data() + std::size_t{y} * info.strideBytes, rowBytes);
    }
    return image;
}

QRectF fitRect(QSizeF content, QSizeF bounds) {
    if (content.width() <= 0.0 || content.height() <= 0.0 || bounds.width() <= 0.0 ||
        bounds.height() <= 0.0) {
        return {};
    }
    const qreal scale =
        std::min(bounds.width() / content.width(), bounds.height() / content.height());
    const qreal w = content.width() * scale;
    const qreal h = content.height() * scale;
    return {(bounds.width() - w) / 2.0, (bounds.height() - h) / 2.0, w, h};
}

} // namespace vsort::hmi

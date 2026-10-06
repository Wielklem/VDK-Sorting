#pragma once

#include <QImage>
#include <QRectF>
#include <QSizeF>

#include <vsort/ipc/preview_ring.hpp>

namespace vsort::hmi {

// Mono8 -> Grayscale8, Rgb8 -> RGB888. Copies row by row (honours stride).
// Returns a null image when the frame is inconsistent (size, stride or pixel count).
[[nodiscard]] QImage toImage(const ipc::PreviewFrame& frame);

// Largest rect with the aspect ratio of `content` that fits centred in `bounds`.
// Null rect when either size is empty.
[[nodiscard]] QRectF fitRect(QSizeF content, QSizeF bounds);

} // namespace vsort::hmi

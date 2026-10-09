#include "video/frame_convert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace vsort::hmi {

namespace {
constexpr std::uint32_t kMaxDimension = 16384U;
constexpr int kHsvShift = 12; // fixed point of the OpenCV 8-bit HSV conversion
constexpr int kHueBins = 180; // H 0..179

// OpenCV's division tables: round((255 << 12) / v) and round((180 << 12) / (6 * diff)).
struct HsvTables {
    std::array<int, 256> sDiv{};
    std::array<int, 256> hDiv{};
    HsvTables() {
        for (std::size_t i = 1; i < 256; ++i) {
            const double d = static_cast<double>(i);
            sDiv.at(i) = static_cast<int>(std::lrint((255 << kHsvShift) / d));
            hDiv.at(i) = static_cast<int>(std::lrint((kHueBins << kHsvShift) / (6.0 * d)));
        }
    }
};

const HsvTables& tables() {
    static const HsvTables t;
    return t;
}

bool inside(const std::array<int, 3>& hsv, const HsvRange& range, std::size_t channel) {
    return hsv.at(channel) >= range.lower.at(channel) && hsv.at(channel) <= range.upper.at(channel);
}

// 50 % green over an RGB888 pixel.
void markGreen(uchar* p) {
    p[0] = static_cast<uchar>(p[0] / 2);
    p[1] = static_cast<uchar>((p[1] + 255) / 2);
    p[2] = static_cast<uchar>(p[2] / 2);
}
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

std::array<int, 3> rgbToHsv(int r, int g, int b) noexcept {
    const auto& t = tables();
    const int v = std::max({r, g, b});
    const int diff = v - std::min({r, g, b});
    const int vr = v == r ? -1 : 0;
    const int vg = v == g ? -1 : 0;
    const int s =
        (diff * t.sDiv.at(static_cast<std::size_t>(v)) + (1 << (kHsvShift - 1))) >> kHsvShift;
    int h = (vr & (g - b)) + (~vr & ((vg & (b - r + 2 * diff)) + ((~vg) & (r - g + 4 * diff))));
    h = (h * t.hDiv.at(static_cast<std::size_t>(diff)) + (1 << (kHsvShift - 1))) >> kHsvShift;
    h += h < 0 ? kHueBins : 0;
    return {h, s, v};
}

QImage channelMask(const QImage& image, int channel, const HsvRange& range,
                   std::vector<int>& histogram) {
    const auto c = static_cast<std::size_t>(std::clamp(channel, 0, 2));
    histogram.assign(c == 0 ? kHueBins : 256, 0);
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    QImage out{rgb.size(), QImage::Format_RGB888};
    for (int y = 0; y < rgb.height(); ++y) {
        const uchar* p = rgb.constScanLine(y);
        uchar* q = out.scanLine(y);
        for (int x = 0; x < rgb.width(); ++x, p += 3, q += 3) {
            const auto hsv = rgbToHsv(p[0], p[1], p[2]);
            const int value = hsv.at(c);
            ++histogram.at(static_cast<std::size_t>(value));
            const auto grey = static_cast<uchar>(c == 0 ? value * 255 / (kHueBins - 1) : value);
            q[0] = q[1] = q[2] = grey;
            if (inside(hsv, range, c)) {
                markGreen(q);
            }
        }
    }
    return out;
}

QImage rangeOverlay(const QImage& image, const HsvRange& range, std::size_t& inRange) {
    inRange = 0;
    QImage out = image.convertToFormat(QImage::Format_RGB888);
    for (int y = 0; y < out.height(); ++y) {
        uchar* p = out.scanLine(y);
        for (int x = 0; x < out.width(); ++x, p += 3) {
            const auto hsv = rgbToHsv(p[0], p[1], p[2]);
            if (inside(hsv, range, 0) && inside(hsv, range, 1) && inside(hsv, range, 2)) {
                ++inRange;
                markGreen(p);
            }
        }
    }
    return out;
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

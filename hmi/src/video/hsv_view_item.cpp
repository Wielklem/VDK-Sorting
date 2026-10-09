#include "video/hsv_view_item.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <QtQml/qqml.h>

namespace vsort::hmi {

namespace {
// Registered by hand, like VideoItem (see video_item.cpp).
void registerHsvViewItem() {
    qmlRegisterType<HsvViewItem>("VsortHmi", 1, 0, "HsvViewItem");
}

constexpr std::array<int, 3> kMax{179, 255, 255};

QList<int> toList(const std::array<int, 3>& v) {
    return {v[0], v[1], v[2]};
}

// Missing values keep `fallback`; values are clamped to H 0..179, S and V 0..255.
std::array<int, 3> fromList(const QList<int>& list, const std::array<int, 3>& fallback) {
    std::array<int, 3> out = fallback;
    for (std::size_t i = 0; i < 3 && i < static_cast<std::size_t>(list.size()); ++i) {
        out.at(i) = std::clamp(list[static_cast<qsizetype>(i)], 0, kMax.at(i));
    }
    return out;
}
} // namespace

// NOLINTNEXTLINE(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables)
Q_COREAPP_STARTUP_FUNCTION(registerHsvViewItem)

HsvViewItem::HsvViewItem(QQuickItem* parent)
    : VideoItem{parent} {}

void HsvViewItem::setView(int view) {
    const int v = std::clamp(view, 0, kResultView);
    if (v == view_) {
        return;
    }
    view_ = v;
    emit viewChanged();
    redraw();
}

QList<int> HsvViewItem::hsvLower() const {
    return toList(range_.lower);
}

void HsvViewItem::setHsvLower(const QList<int>& hsv) {
    const auto value = fromList(hsv, range_.lower);
    if (value == range_.lower) {
        return;
    }
    range_.lower = value;
    emit rangeChanged();
    redraw();
}

QList<int> HsvViewItem::hsvUpper() const {
    return toList(range_.upper);
}

void HsvViewItem::setHsvUpper(const QList<int>& hsv) {
    const auto value = fromList(hsv, range_.upper);
    if (value == range_.upper) {
        return;
    }
    range_.upper = value;
    emit rangeChanged();
    redraw();
}

QImage HsvViewItem::present(const QImage& frame) {
    const auto pixels = static_cast<double>(frame.width()) * frame.height();
    QImage out;
    QList<int> histogram;
    double percent = 0.0;
    if (view_ == kResultView) {
        std::size_t inRange = 0;
        out = rangeOverlay(frame, range_, inRange);
        percent = pixels > 0.0 ? 100.0 * static_cast<double>(inRange) / pixels : 0.0;
    } else {
        std::vector<int> counts;
        out = channelMask(frame, view_, range_, counts);
        const auto c = static_cast<std::size_t>(view_);
        std::int64_t inRange = 0;
        for (std::size_t v = 0; v < counts.size(); ++v) {
            if (static_cast<int>(v) >= range_.lower.at(c) &&
                static_cast<int>(v) <= range_.upper.at(c)) {
                inRange += counts[v];
            }
        }
        histogram = QList<int>(counts.begin(), counts.end());
        percent = pixels > 0.0 ? 100.0 * static_cast<double>(inRange) / pixels : 0.0;
    }
    // Render thread: hand the statistics to the GUI thread (dropped if the item is gone).
    QMetaObject::invokeMethod(
        this, [this, histogram, percent] { publish(histogram, percent); }, Qt::QueuedConnection);
    return out;
}

void HsvViewItem::publish(const QList<int>& histogram, double percent) {
    histogram_ = histogram;
    inRangePercent_ = percent;
    emit statsChanged();
}

} // namespace vsort::hmi

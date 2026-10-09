#include "live/detector_model.hpp"

#include <QString>
#include <QVariantMap>
#include <deque>

namespace vsort::hmi {

DetectorModel::DetectorModel(ServiceClient& client, QObject* parent)
    : QObject{parent} {
    connect(&client, &ServiceClient::analysisOverlayReceived, this, &DetectorModel::onOverlay);
}

void DetectorModel::selectCamera(int cameraId) {
    if (cameraId == cameraId_) {
        return;
    }
    cameraId_ = cameraId;
    const bool was = receiving();
    history_.clear();
    emit cameraIdChanged();
    if (was) {
        emit receivingChanged();
    }
    rebuild();
}

void DetectorModel::setFrameId(qint64 frameId) {
    if (frameId == frameId_) {
        return;
    }
    frameId_ = frameId;
    rebuild();
}

void DetectorModel::onOverlay(const AnalysisOverlayData& overlay) {
    if (static_cast<int>(overlay.cameraId) != cameraId_) {
        return;
    }
    const bool was = receiving();
    std::erase_if(history_, [&](const AnalysisOverlayData& o) {
        return o.sensorId == overlay.sensorId && o.frameId == overlay.frameId;
    });
    history_.push_back(overlay);
    while (history_.size() > kHistory) {
        history_.pop_front();
    }
    if (!was) {
        emit receivingChanged();
    }
    if (static_cast<qint64>(overlay.frameId) == frameId_) {
        rebuild();
    }
}

void DetectorModel::rebuild() {
    objects_.clear();
    frameSize_ = {};
    matched_ = false;
    counted_ = 0;
    neighbours_ = 0;
    for (const auto& overlay : history_) {
        if (static_cast<qint64>(overlay.frameId) != frameId_) {
            continue;
        }
        matched_ = true;
        frameSize_ =
            QSize{static_cast<int>(overlay.frameWidth), static_cast<int>(overlay.frameHeight)};
        for (const auto& o : overlay.objects) {
            QVariantList points;
            points.reserve(o.contour.size());
            for (const int v : o.contour) {
                points.push_back(v);
            }
            const QString label = o.counted ? QStringLiteral("%1 × %2 mm")
                                                  .arg(o.lengthMm, 0, 'f', 1)
                                                  .arg(o.widthMm, 0, 'f', 1)
                                            : QString{};
            objects_.push_back(QVariantMap{{QStringLiteral("points"), points},
                                           {QStringLiteral("counted"), o.counted},
                                           {QStringLiteral("label"), label}});
            ++(o.counted ? counted_ : neighbours_);
        }
    }
    emit objectsChanged();
}

} // namespace vsort::hmi

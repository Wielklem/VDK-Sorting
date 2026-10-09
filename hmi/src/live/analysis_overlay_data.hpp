#pragma once

#include <QList>
#include <QRect>
#include <QVector>
#include <QtGlobal>

namespace vsort::hmi {

// P60.90: one object the service's segmentation found (AnalysisOverlayEvent, MSG-60-02).
struct DetectedObjectData {
    QList<int> contour;  // x0, y0, x1, y1, ... in camera-frame pixels
    bool counted{false}; // centred in the lane ROI; false = belongs to a neighbour cup
    double lengthMm{0.0};
    double widthMm{0.0};

    bool operator==(const DetectedObjectData&) const = default;
};

// What the analysis found in one frame of one sensor.
struct AnalysisOverlayData {
    quint16 cameraId{0};
    quint16 sensorId{0};
    quint64 frameId{0};
    quint32 frameWidth{0};
    quint32 frameHeight{0};
    QRect roi; // lane ROI in frame pixels
    QVector<DetectedObjectData> objects;
};

} // namespace vsort::hmi

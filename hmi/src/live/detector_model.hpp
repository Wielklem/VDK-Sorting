#pragma once

#include <QObject>
#include <QSize>
#include <QVariantList>
#include <deque>

#include "live/analysis_overlay_data.hpp"
#include "live/service_client.hpp"

namespace vsort::hmi {

// Model behind G60.30 Detector (P60.90): what the service's pipeline found in the frame the HMI
// shows. The service sends one AnalysisOverlayEvent per analysed frame and sensor; the model keeps
// the newest ones of the selected camera and picks those whose frame ID is the shown preview frame
// (VideoItem.frameId), so the contours stay on the picture, also while frozen.
class DetectorModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int cameraId READ cameraId NOTIFY cameraIdChanged)
    Q_PROPERTY(qint64 frameId READ frameId WRITE setFrameId NOTIFY objectsChanged)
    // [{points: [x0, y0, ...] (frame pixels), counted, label}] of every sensor of the camera.
    Q_PROPERTY(QVariantList objects READ objects NOTIFY objectsChanged)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY objectsChanged) // what `points` refer to
    Q_PROPERTY(bool matched READ matched NOTIFY objectsChanged)      // results for the shown frame
    Q_PROPERTY(int countedObjects READ countedObjects NOTIFY objectsChanged)
    Q_PROPERTY(int neighbourObjects READ neighbourObjects NOTIFY objectsChanged)
    Q_PROPERTY(bool receiving READ receiving NOTIFY receivingChanged) // results for this camera

public:
    static constexpr std::size_t kHistory = 64; // frames kept per camera

    // `client` must outlive the model.
    explicit DetectorModel(ServiceClient& client, QObject* parent = nullptr);

    [[nodiscard]] int cameraId() const noexcept { return cameraId_; }
    [[nodiscard]] qint64 frameId() const noexcept { return frameId_; }
    void setFrameId(qint64 frameId);
    [[nodiscard]] QVariantList objects() const { return objects_; }
    [[nodiscard]] QSize frameSize() const noexcept { return frameSize_; }
    [[nodiscard]] bool matched() const noexcept { return matched_; }
    [[nodiscard]] int countedObjects() const noexcept { return counted_; }
    [[nodiscard]] int neighbourObjects() const noexcept { return neighbours_; }
    [[nodiscard]] bool receiving() const noexcept { return !history_.empty(); }

    Q_INVOKABLE void selectCamera(int cameraId);

signals:
    void cameraIdChanged();
    void objectsChanged();
    void receivingChanged();

private:
    void onOverlay(const AnalysisOverlayData& overlay);
    void rebuild();

    int cameraId_{-1};
    qint64 frameId_{-1};
    std::deque<AnalysisOverlayData> history_; // selected camera, newest last
    QVariantList objects_;
    QSize frameSize_;
    bool matched_{false};
    int counted_{0};
    int neighbours_{0};
};

} // namespace vsort::hmi

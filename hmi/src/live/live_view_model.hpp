#pragma once

#include <QAbstractListModel>
#include <QTimer>
#include <QVector>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <vsort/ipc/preview_ring.hpp>

#include "live/camera_info.hpp"
#include "live/service_client.hpp"
#include "video/frame_sink.hpp"

namespace vsort::hmi {

// Model behind the live view page (G30.10): one row per camera. It attaches to the preview ring
// of every camera and pulls the newest frame on a render timer. Frames go to the VideoItems that
// QML attached with attachVideo(). Freeze is a display-only state of the HMI: a frozen camera
// is not read (its VideoItems keep the last picture), the service keeps streaming.
// The frame rates (P30.86) come from the service: frames the camera delivered and frames analysed,
// not the throttled preview. -1 = not known (no rates yet, or a camera without a sensor).
class LiveViewModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool allFrozen READ allFrozen NOTIFY allFrozenChanged)
    Q_PROPERTY(int cameraCount READ cameraCount NOTIFY cameraCountChanged)

public:
    enum Role {
        CameraIdRole = Qt::UserRole + 1,
        SerialRole,
        LinkStateRole,
        FrozenRole,
        HasFrameRole,
        FrameCounterRole,
        FrameWidthRole,
        FrameHeightRole,
        IncomingFpsRole,
        AnalysedFpsRole
    };

    // `client` must outlive the model.
    explicit LiveViewModel(ServiceClient& client, QObject* parent = nullptr);
    ~LiveViewModel() override;
    LiveViewModel(const LiveViewModel&) = delete;
    LiveViewModel& operator=(const LiveViewModel&) = delete;
    LiveViewModel(LiveViewModel&&) = delete;
    LiveViewModel& operator=(LiveViewModel&&) = delete;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool connected() const noexcept { return client_.connected(); }
    [[nodiscard]] bool allFrozen() const noexcept;
    [[nodiscard]] int cameraCount() const noexcept { return static_cast<int>(entries_.size()); }

    Q_INVOKABLE void setFrozen(int cameraId, bool frozen);
    Q_INVOKABLE void toggleFrozen(int cameraId);
    Q_INVOKABLE void setAllFrozen(bool frozen);

    // `item` must be an IFrameSink (VideoItem). It gets this camera's frames until it is
    // destroyed or detached, and the newest frame right away. Other objects are ignored.
    Q_INVOKABLE void attachVideo(int cameraId, QObject* item);
    Q_INVOKABLE void detachVideo(QObject* item);

    // One render step: pulls the newest frame of every camera that is not frozen.
    void renderTick();

signals:
    void connectedChanged();
    void allFrozenChanged();
    void cameraCountChanged();

private:
    struct Entry {
        CameraInfo info;
        bool frozen{false};
        std::unique_ptr<ipc::PreviewRingReader> reader;
        QString attachedName;
        std::uint64_t lastSeen{0};
        quint64 frameCounter{0};
        int frameWidth{0};
        int frameHeight{0};
        double incomingFps{-1.0};
        double analysedFps{-1.0};
    };
    struct Sink {
        std::uint16_t cameraId{0};
        QObject* object{nullptr}; // identity only; removed from sinks_ when destroyed
        IFrameSink* sink{nullptr};
    };

    [[nodiscard]] int rowOf(std::uint16_t cameraId) const noexcept;
    void onCameraList(const QVector<CameraInfo>& cameras);
    void onCameraUpdated(const CameraInfo& camera);
    void onStreamChanged(quint16 cameraId, const QString& shmName);
    void onConnectedChanged(bool connected);
    void onCameraRates(const QVector<CameraRateData>& rates);
    void updateAllFrozen();
    void deliver(std::uint16_t cameraId, const FramePtr& frame);
    static void attach(Entry& entry);
    void notifyRow(int row, const QList<int>& roles);

    ServiceClient& client_;
    std::vector<Entry> entries_;
    std::vector<Sink> sinks_;
    std::map<std::uint16_t, FramePtr> latest_; // newest frame per camera, for late attachers
    std::set<std::uint16_t> frozenIds_;        // survives camera list refreshes
    QTimer renderTimer_;
    bool lastAllFrozen_{false};
};

} // namespace vsort::hmi

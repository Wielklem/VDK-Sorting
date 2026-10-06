#pragma once

#include <QObject>
#include <QString>

#include "live/camera_info.hpp"
#include "live/service_client.hpp"

namespace vsort::hmi {

// Model behind G30.30 camera settings: exposure and gain of one camera. The service applies them
// to the camera at once. Everything else (ROI, trigger) is sent back unchanged.
class CameraSettingsModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int cameraId READ cameraId NOTIFY cameraIdChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double exposureUs READ exposureUs NOTIFY settingsChanged)
    Q_PROPERTY(double gainDb READ gainDb NOTIFY settingsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    // `client` must outlive the model.
    explicit CameraSettingsModel(ServiceClient& client, QObject* parent = nullptr);
    ~CameraSettingsModel() override = default;
    CameraSettingsModel(const CameraSettingsModel&) = delete;
    CameraSettingsModel& operator=(const CameraSettingsModel&) = delete;
    CameraSettingsModel(CameraSettingsModel&&) = delete;
    CameraSettingsModel& operator=(CameraSettingsModel&&) = delete;

    [[nodiscard]] int cameraId() const noexcept { return cameraId_; }
    [[nodiscard]] bool loaded() const noexcept { return loaded_; }
    [[nodiscard]] bool busy() const noexcept { return busy_; }
    [[nodiscard]] double exposureUs() const noexcept { return settings_.exposureUs; }
    [[nodiscard]] double gainDb() const noexcept { return settings_.gainDb; }
    [[nodiscard]] QString status() const { return status_; }

    Q_INVOKABLE void load(int cameraId); // reads the settings of this camera
    Q_INVOKABLE void apply(double exposureUs, double gainDb);

signals:
    void cameraIdChanged();
    void loadedChanged();
    void busyChanged();
    void settingsChanged();
    void statusChanged();

private:
    void onReceived(quint16 cameraId, const CameraSettingsData& settings);
    void onApplied();
    void onFailed(const QString& what);
    void setLoaded(bool loaded);
    void setBusy(bool busy);
    void setStatus(const QString& status);

    ServiceClient& client_;
    CameraSettingsData settings_;
    int cameraId_{-1};
    bool loaded_{false};
    bool busy_{false};
    bool applying_{false};
    QString status_;
};

} // namespace vsort::hmi

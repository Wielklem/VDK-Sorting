#include "live/camera_settings_model.hpp"

namespace vsort::hmi {

CameraSettingsModel::CameraSettingsModel(ServiceClient& client, QObject* parent)
    : QObject{parent}
    , client_{client} {
    connect(&client_, &ServiceClient::cameraSettingsReceived, this,
            &CameraSettingsModel::onReceived);
    connect(&client_, &ServiceClient::cameraSettingsApplied, this, &CameraSettingsModel::onApplied);
    connect(&client_, &ServiceClient::commandFailed, this, &CameraSettingsModel::onFailed);
}

void CameraSettingsModel::load(int cameraId) {
    if (cameraId < 0 || cameraId > 0xFFFF) {
        return;
    }
    if (cameraId != cameraId_) {
        cameraId_ = cameraId;
        emit cameraIdChanged();
    }
    applying_ = false;
    setLoaded(false);
    if (!client_.connected()) {
        setBusy(false);
        setStatus(QStringLiteral("Service offline"));
        return;
    }
    setStatus({});
    setBusy(true);
    client_.requestCameraSettings(static_cast<quint16>(cameraId));
}

void CameraSettingsModel::apply(double exposureUs, double gainDb) {
    if (!loaded_ || busy_ || cameraId_ < 0) {
        return;
    }
    CameraSettingsData next = settings_;
    next.exposureUs = exposureUs;
    next.gainDb = gainDb;
    applying_ = true;
    setBusy(true);
    setStatus(QStringLiteral("Applying..."));
    client_.setCameraSettings(static_cast<quint16>(cameraId_), next);
}

void CameraSettingsModel::onReceived(quint16 cameraId, const CameraSettingsData& settings) {
    if (cameraId_ < 0 || static_cast<int>(cameraId) != cameraId_) {
        return;
    }
    settings_ = settings;
    setStatus(applying_ ? QStringLiteral("Applied") : QString{});
    applying_ = false;
    setLoaded(true);
    setBusy(false);
    emit settingsChanged();
}

void CameraSettingsModel::onApplied() {
    if (applying_ && cameraId_ >= 0) {
        client_.requestCameraSettings(
            static_cast<quint16>(cameraId_)); // read back what the camera took
    }
}

void CameraSettingsModel::onFailed(const QString& what) {
    if (!busy_) {
        return;
    }
    applying_ = false;
    setBusy(false);
    setStatus(what);
}

void CameraSettingsModel::setLoaded(bool loaded) {
    if (loaded_ == loaded) {
        return;
    }
    loaded_ = loaded;
    emit loadedChanged();
}

void CameraSettingsModel::setBusy(bool busy) {
    if (busy_ == busy) {
        return;
    }
    busy_ = busy;
    emit busyChanged();
}

void CameraSettingsModel::setStatus(const QString& status) {
    if (status_ == status) {
        return;
    }
    status_ = status;
    emit statusChanged();
}

} // namespace vsort::hmi

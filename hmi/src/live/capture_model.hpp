#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QtGlobal>

#include "live/capture_data.hpp"
#include "live/service_client.hpp"

namespace vsort::hmi {

// Model behind the Capture tab (P60.100): saves photos of the lane ROIs to a folder on the service
// PC, to review how the analysis judged them (are the "empty" cups really empty?). One row per
// sensor, ordered by camera, with its own on/off choice and progress. The settings are only held
// here (not remembered after the HMI is closed). The service does the saving; while a capture
// runs, this model asks for its status about once per second.
// `mode` is 0 no potato, 1 single, 2 multiple, 3 size range (length of the largest object).
class CaptureModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY canStartChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString sessionFolder READ sessionFolder NOTIFY stateChanged)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY settingsChanged)
    Q_PROPERTY(
        int photosPerSensor READ photosPerSensor WRITE setPhotosPerSensor NOTIFY settingsChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY settingsChanged)
    Q_PROPERTY(double minMm READ minMm WRITE setMinMm NOTIFY settingsChanged)
    Q_PROPERTY(double maxMm READ maxMm WRITE setMaxMm NOTIFY settingsChanged)

public:
    enum Role {
        SensorIdRole = Qt::UserRole + 1,
        CameraIdRole,
        SelectedRole,
        CameraSelectedRole, // every sensor of this row's camera is selected
        SavedRole,
        TargetRole,
        FirstOfCameraRole // first row of its camera: the page shows the camera header here
    };

    static constexpr int kDefaultPhotos = 50;
    static constexpr int kMaxPhotos = 100000;

    // `client` must outlive the model. `pollMs`: status interval while a capture runs.
    explicit CaptureModel(ServiceClient& client, int pollMs = 1000, QObject* parent = nullptr);
    ~CaptureModel() override = default;
    CaptureModel(const CaptureModel&) = delete;
    CaptureModel& operator=(const CaptureModel&) = delete;
    CaptureModel(CaptureModel&&) = delete;
    CaptureModel& operator=(CaptureModel&&) = delete;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool running() const noexcept { return state_ == CaptureStateData::Running; }
    [[nodiscard]] bool busy() const noexcept { return busy_; }
    [[nodiscard]] bool canStart() const noexcept { return canStart_; }
    [[nodiscard]] QString statusText() const { return statusText_; }
    [[nodiscard]] QString sessionFolder() const { return sessionFolder_; }
    [[nodiscard]] QString folder() const { return folder_; }
    [[nodiscard]] int photosPerSensor() const noexcept { return photos_; }
    [[nodiscard]] int mode() const noexcept { return mode_; }
    [[nodiscard]] double minMm() const noexcept { return minMm_; }
    [[nodiscard]] double maxMm() const noexcept { return maxMm_; }

    void setFolder(const QString& folder);
    void setPhotosPerSensor(int photos);
    void setMode(int mode);
    void setMinMm(double mm);
    void setMaxMm(double mm);

    Q_INVOKABLE void refresh(); // asks the service for its capture status
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    // Sensor and camera choices; ignored while a capture runs.
    Q_INVOKABLE void setSelected(int row, bool selected);
    Q_INVOKABLE void setCameraSelected(int cameraId, bool selected);
    Q_INVOKABLE void selectAll(bool selected);

signals:
    void stateChanged();
    void settingsChanged();
    void canStartChanged();

private:
    struct Row {
        quint16 sensorId{0};
        quint16 cameraId{0};
        bool selected{true};
        quint32 saved{0};
        quint32 target{0};
    };

    void onStatus(const CaptureStatusData& status);
    void onFailed(const QString& what);
    void onConnectedChanged();
    void mergeSensors(const CaptureStatusData& status);
    void selectionChanged();
    void refreshState();
    [[nodiscard]] bool locked() const noexcept { return running() || busy_; }
    [[nodiscard]] bool computeCanStart() const;
    [[nodiscard]] QString buildStatusText() const;
    [[nodiscard]] QVector<quint16> selectedIds() const;

    ServiceClient& client_;
    QTimer poll_;
    QVector<Row> rows_;
    CaptureStateData state_{CaptureStateData::Idle};
    bool busy_{false}; // a start or stop is on its way
    bool canStart_{false};
    QString message_; // last failed command; cleared by the next status
    QString errorText_;
    QString sessionFolder_;
    QString statusText_;
    QString folder_;
    int photos_{kDefaultPhotos};
    int mode_{0};
    double minMm_{0.0};
    double maxMm_{0.0};
};

} // namespace vsort::hmi

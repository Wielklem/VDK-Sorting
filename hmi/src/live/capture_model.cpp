#include "live/capture_model.hpp"

#include <QHash>
#include <algorithm>

namespace vsort::hmi {

namespace {
constexpr int kSizeRange = 3;
constexpr int kLastMode = 4; // all photos
} // namespace

CaptureModel::CaptureModel(ServiceClient& client, int pollMs, QObject* parent)
    : QAbstractListModel{parent}
    , client_{client} {
    poll_.setInterval(pollMs);
    connect(&poll_, &QTimer::timeout, this, [this] { client_.requestCaptureStatus(); });
    connect(&client_, &ServiceClient::captureStatusReceived, this, &CaptureModel::onStatus);
    connect(&client_, &ServiceClient::commandFailed, this, &CaptureModel::onFailed);
    connect(&client_, &ServiceClient::connectedChanged, this, &CaptureModel::onConnectedChanged);
    refreshState();
}

int CaptureModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant CaptureModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
        return {};
    }
    const Row& row = rows_.at(index.row());
    switch (role) {
    case SensorIdRole:
        return row.sensorId;
    case CameraIdRole:
        return row.cameraId;
    case SelectedRole:
        return row.selected;
    case CameraSelectedRole:
        return std::ranges::all_of(rows_, [&row](const Row& other) {
            return other.cameraId != row.cameraId || other.selected;
        });
    case SavedRole:
        return row.saved;
    case TargetRole:
        return row.target;
    case FirstOfCameraRole:
        return index.row() == 0 || rows_.at(index.row() - 1).cameraId != row.cameraId;
    case SensorNameRole:
        return row.sensorName;
    case LaneNameRole:
        return row.laneName;
    default:
        return {};
    }
}

QHash<int, QByteArray> CaptureModel::roleNames() const {
    return {{SensorIdRole, "sensorId"},
            {CameraIdRole, "cameraId"},
            {SelectedRole, "selected"},
            {CameraSelectedRole, "cameraSelected"},
            {SavedRole, "saved"},
            {TargetRole, "target"},
            {FirstOfCameraRole, "firstOfCamera"},
            {SensorNameRole, "sensorName"},
            {LaneNameRole, "laneName"}};
}

void CaptureModel::setFolder(const QString& folder) {
    if (folder_ == folder) {
        return;
    }
    folder_ = folder;
    emit settingsChanged();
    refreshState();
}

void CaptureModel::setPhotosPerSensor(int photos) {
    photos = std::clamp(photos, 1, kMaxPhotos);
    if (photos_ == photos) {
        return;
    }
    photos_ = photos;
    emit settingsChanged();
    refreshState();
}

void CaptureModel::setMode(int mode) {
    mode = std::clamp(mode, 0, kLastMode);
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    emit settingsChanged();
    refreshState();
}

void CaptureModel::setMinMm(double mm) {
    if (minMm_ == mm) {
        return;
    }
    minMm_ = mm;
    emit settingsChanged();
    refreshState();
}

void CaptureModel::setMaxMm(double mm) {
    if (maxMm_ == mm) {
        return;
    }
    maxMm_ = mm;
    emit settingsChanged();
    refreshState();
}

void CaptureModel::refresh() {
    if (client_.connected()) {
        client_.requestCaptureStatus();
    }
}

void CaptureModel::start() {
    if (!canStart_) {
        return;
    }
    message_.clear();
    busy_ = true;
    refreshState();
    client_.startCapture(CaptureStartData{.folder = folder_.trimmed(),
                                          .photosPerSensor = static_cast<quint32>(photos_),
                                          .mode = mode_ + 1,
                                          .minMm = minMm_,
                                          .maxMm = maxMm_,
                                          .sensorIds = selectedIds()});
}

void CaptureModel::stop() {
    if (!running() || busy_) {
        return;
    }
    busy_ = true;
    refreshState();
    client_.stopCapture();
}

void CaptureModel::setSelected(int row, bool selected) {
    if (locked() || row < 0 || row >= rows_.size() || rows_.at(row).selected == selected) {
        return;
    }
    rows_[row].selected = selected;
    selectionChanged();
}

void CaptureModel::setCameraSelected(int cameraId, bool selected) {
    if (locked()) {
        return;
    }
    for (Row& row : rows_) {
        if (row.cameraId == cameraId) {
            row.selected = selected;
        }
    }
    selectionChanged();
}

void CaptureModel::selectAll(bool selected) {
    if (locked()) {
        return;
    }
    for (Row& row : rows_) {
        row.selected = selected;
    }
    selectionChanged();
}

void CaptureModel::selectionChanged() {
    if (!rows_.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1),
                         {SelectedRole, CameraSelectedRole});
    }
    refreshState();
}

void CaptureModel::onStatus(const CaptureStatusData& status) {
    busy_ = false;
    message_.clear();
    state_ = status.state;
    sessionFolder_ = status.sessionFolder;
    errorText_ = status.errorText;
    if (folder_.isEmpty() && !status.defaultFolder.isEmpty()) {
        folder_ = status.defaultFolder; // the user has not chosen a folder yet
        emit settingsChanged();
    }
    mergeSensors(status);
    if (running()) {
        poll_.start();
    } else {
        poll_.stop();
    }
    refreshState();
}

void CaptureModel::onFailed(const QString& what) {
    if (!busy_) {
        return; // not the answer to our start or stop
    }
    busy_ = false;
    message_ = what;
    refreshState();
}

void CaptureModel::onConnectedChanged() {
    if (client_.connected()) {
        refresh();
    } else {
        poll_.stop();
    }
    refreshState();
}

// Rows follow the sensors of the service, ordered by camera. Before a session the choice of
// sensors is the user's; during and after one it is the session's.
void CaptureModel::mergeSensors(const CaptureStatusData& status) {
    QVector<CaptureSensorData> sensors = status.sensors;
    std::ranges::stable_sort(sensors, [](const CaptureSensorData& a, const CaptureSensorData& b) {
        return a.cameraId != b.cameraId ? a.cameraId < b.cameraId : a.sensorId < b.sensorId;
    });
    const bool fromSession = status.state != CaptureStateData::Idle;
    bool sameSensors = sensors.size() == rows_.size();
    for (qsizetype i = 0; sameSensors && i < sensors.size(); ++i) {
        sameSensors = sensors.at(i).sensorId == rows_.at(i).sensorId &&
                      sensors.at(i).cameraId == rows_.at(i).cameraId;
    }
    if (sameSensors) {
        for (qsizetype i = 0; i < sensors.size(); ++i) {
            Row& row = rows_[i];
            row.saved = sensors.at(i).saved;
            row.target = sensors.at(i).target;
            row.sensorName = sensors.at(i).name;
            row.laneName = sensors.at(i).laneName;
            if (fromSession) {
                row.selected = sensors.at(i).selected;
            }
        }
        if (!rows_.isEmpty()) {
            emit dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1));
        }
        return;
    }
    QHash<quint16, bool> before;
    for (const Row& row : rows_) {
        before.insert(row.sensorId, row.selected);
    }
    beginResetModel();
    rows_.clear();
    for (const auto& s : sensors) {
        rows_.push_back(Row{.sensorId = s.sensorId,
                            .cameraId = s.cameraId,
                            .selected = fromSession ? s.selected : before.value(s.sensorId, true),
                            .saved = s.saved,
                            .target = s.target,
                            .sensorName = s.name,
                            .laneName = s.laneName});
    }
    endResetModel();
}

QVector<quint16> CaptureModel::selectedIds() const {
    QVector<quint16> ids;
    for (const Row& row : rows_) {
        if (row.selected) {
            ids.push_back(row.sensorId);
        }
    }
    return ids;
}

bool CaptureModel::computeCanStart() const {
    if (!client_.connected() || locked() || folder_.trimmed().isEmpty() || photos_ < 1) {
        return false;
    }
    if (mode_ == kSizeRange && !(minMm_ >= 0.0 && maxMm_ >= minMm_)) {
        return false;
    }
    return std::ranges::any_of(rows_, [](const Row& row) { return row.selected; });
}

QString CaptureModel::buildStatusText() const {
    if (!message_.isEmpty()) {
        return message_;
    }
    if (!client_.connected()) {
        return QStringLiteral("Service offline");
    }
    quint32 saved = 0;
    quint32 target = 0;
    for (const Row& row : rows_) {
        if (row.selected) {
            saved += row.saved;
            target += row.target;
        }
    }
    switch (state_) {
    case CaptureStateData::Idle:
        return busy_ ? QStringLiteral("Starting...") : QStringLiteral("Ready");
    case CaptureStateData::Running:
        return busy_ ? QStringLiteral("Stopping...")
                     : QStringLiteral("Capturing: %1 of %2 photos saved").arg(saved).arg(target);
    case CaptureStateData::Done:
        return QStringLiteral("Done: %1 photos saved").arg(saved);
    case CaptureStateData::Stopped:
        return QStringLiteral("Stopped: %1 photos saved").arg(saved);
    case CaptureStateData::Failed:
        return QStringLiteral("Failed: %1 (%2 photos saved)").arg(errorText_).arg(saved);
    }
    return {};
}

// Recomputes what the page shows and tells it, when something changed.
void CaptureModel::refreshState() {
    statusText_ = buildStatusText();
    const bool can = computeCanStart();
    const bool canChanged = can != canStart_;
    canStart_ = can;
    emit stateChanged();
    if (canChanged) {
        emit canStartChanged();
    }
}

} // namespace vsort::hmi

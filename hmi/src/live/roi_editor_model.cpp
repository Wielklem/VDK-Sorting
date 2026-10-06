#include "live/roi_editor_model.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QVariantMap>
#include <algorithm>
#include <utility>

#include <vsort/common/roi_config.hpp>

namespace vsort::hmi {
namespace {

constexpr double kMin = RoiEditorModel::kMinSize;

double unit(double value) {
    return std::clamp(value, 0.0, 1.0);
}

QString moduleName() {
    return QString::fromUtf8(kRoiModule.data(), static_cast<qsizetype>(kRoiModule.size()));
}

} // namespace

RoiEditorModel::RoiEditorModel(ServiceClient& client, QObject* parent)
    : QAbstractListModel{parent}
    , client_{client} {
    connect(&client_, &ServiceClient::configReceived, this, &RoiEditorModel::onConfigReceived);
    connect(&client_, &ServiceClient::commandFailed, this, &RoiEditorModel::onFailed);
    connect(&client_, &ServiceClient::connectedChanged, this, [this](bool isConnected) {
        if (isConnected) {
            requestLoad(false);
        }
    });
    if (client_.connected()) {
        requestLoad(false);
    }
}

const std::vector<RoiEntry>& RoiEditorModel::rows() const {
    static const std::vector<RoiEntry> kEmpty;
    const auto it = byCamera_.find(cameraId_);
    return it != byCamera_.end() ? it->second : kEmpty;
}

bool RoiEditorModel::validRow(int row) const {
    return row >= 0 && row < static_cast<int>(rows().size());
}

RoiEntry& RoiEditorModel::at(int row) {
    return byCamera_[cameraId_][static_cast<std::size_t>(row)];
}

int RoiEditorModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows().size());
}

QVariant RoiEditorModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !validRow(index.row())) {
        return {};
    }
    const RoiEntry& roi = rows()[static_cast<std::size_t>(index.row())];
    switch (role) {
    case RoiIdRole:
        return roi.id;
    case RoiNameRole:
        return roi.name;
    case RoiXRole:
        return roi.x;
    case RoiYRole:
        return roi.y;
    case RoiWRole:
        return roi.w;
    case RoiHRole:
        return roi.h;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> RoiEditorModel::roleNames() const {
    return {{RoiIdRole, "roiId"}, {RoiNameRole, "roiName"}, {RoiXRole, "roiX"},
            {RoiYRole, "roiY"},   {RoiWRole, "roiW"},       {RoiHRole, "roiH"}};
}

QRectF RoiEditorModel::selectedRect() const {
    if (!validRow(selected_)) {
        return {};
    }
    const RoiEntry& roi = rows()[static_cast<std::size_t>(selected_)];
    return {roi.x, roi.y, roi.w, roi.h};
}

QVariantList RoiEditorModel::rois() const {
    QVariantList out;
    for (const RoiEntry& roi : rows()) {
        out.push_back(QVariantMap{{QStringLiteral("x"), roi.x},
                                  {QStringLiteral("y"), roi.y},
                                  {QStringLiteral("width"), roi.w},
                                  {QStringLiteral("height"), roi.h},
                                  {QStringLiteral("label"), roi.name}});
    }
    return out;
}

void RoiEditorModel::selectCamera(int cameraId) {
    if (cameraId == cameraId_) {
        return;
    }
    beginResetModel();
    cameraId_ = cameraId;
    selected_ = -1;
    endResetModel();
    emit cameraIdChanged();
    emit selectedChanged();
    emit selectedRectChanged();
    emit roisChanged();
}

void RoiEditorModel::select(int row) {
    setSelected(validRow(row) ? row : -1);
}

int RoiEditorModel::addRoi(double x1, double y1, double x2, double y2) {
    if (cameraId_ < 0) {
        return -1;
    }
    const double left = unit(std::min(x1, x2));
    const double top = unit(std::min(y1, y2));
    const double right = unit(std::max(x1, x2));
    const double bottom = unit(std::max(y1, y2));
    if (right - left < kMin || bottom - top < kMin) {
        return -1;
    }
    auto& list = byCamera_[cameraId_];
    int nextId = 1;
    for (const RoiEntry& roi : list) {
        nextId = std::max(nextId, roi.id + 1);
    }
    const int row = static_cast<int>(list.size());
    beginInsertRows({}, row, row);
    list.push_back(RoiEntry{.id = nextId,
                            .name = QStringLiteral("ROI %1").arg(nextId),
                            .x = left,
                            .y = top,
                            .w = right - left,
                            .h = bottom - top});
    endInsertRows();
    setSelected(row);
    emit roisChanged();
    setDirty(true);
    return row;
}

void RoiEditorModel::removeRoi(int row) {
    if (!validRow(row)) {
        return;
    }
    auto& list = byCamera_[cameraId_];
    beginRemoveRows({}, row, row);
    list.erase(list.begin() + row);
    endRemoveRows();
    if (selected_ == row) {
        selected_ = -1;
    } else if (selected_ > row) {
        --selected_;
    }
    emit selectedChanged();
    emit selectedRectChanged();
    emit roisChanged();
    setDirty(true);
}

void RoiEditorModel::moveTo(int row, double x, double y) {
    if (!validRow(row)) {
        return;
    }
    RoiEntry& roi = at(row);
    const double newX = std::clamp(x, 0.0, 1.0 - roi.w);
    const double newY = std::clamp(y, 0.0, 1.0 - roi.h);
    if (newX == roi.x && newY == roi.y) {
        return;
    }
    roi.x = newX;
    roi.y = newY;
    changed(row);
}

void RoiEditorModel::setRect(int row, double x, double y, double w, double h) {
    if (!validRow(row)) {
        return;
    }
    RoiEntry& roi = at(row);
    const double newX = std::clamp(x, 0.0, 1.0 - kMin);
    const double newY = std::clamp(y, 0.0, 1.0 - kMin);
    const double newW = std::clamp(w, kMin, 1.0 - newX);
    const double newH = std::clamp(h, kMin, 1.0 - newY);
    if (newX == roi.x && newY == roi.y && newW == roi.w && newH == roi.h) {
        return;
    }
    roi.x = newX;
    roi.y = newY;
    roi.w = newW;
    roi.h = newH;
    changed(row);
}

void RoiEditorModel::reload() {
    requestLoad(true);
}

void RoiEditorModel::requestLoad(bool force) {
    inFlight_.push_back({.save = false, .force = force});
    setStatus(QStringLiteral("Loading..."));
    client_.requestConfig(moduleName());
}

void RoiEditorModel::save() {
    if (!client_.connected()) {
        setStatus(QStringLiteral("Service offline"));
        return;
    }
    inFlight_.push_back({.save = true, .force = false});
    setStatus(QStringLiteral("Saving..."));
    client_.setConfig(moduleName(), toJson());
}

void RoiEditorModel::onConfigReceived(const QString& module, const QByteArray& json,
                                      quint32 version) {
    if (module != moduleName()) {
        return;
    }
    Pending kind;
    if (!inFlight_.empty()) {
        kind = inFlight_.front();
        inFlight_.pop_front();
    }
    if (!kind.save && dirty_ && !kind.force) {
        return; // a background load must not overwrite unsaved edits
    }
    Config parsed;
    if (!parseConfig(json, parsed)) {
        setStatus(QStringLiteral("Invalid ROI config from service"));
        return;
    }
    beginResetModel();
    byCamera_ = std::move(parsed);
    selected_ = -1;
    endResetModel();
    if (!loaded_) {
        loaded_ = true;
        emit loadedChanged();
    }
    emit selectedChanged();
    emit selectedRectChanged();
    emit roisChanged();
    setDirty(false);
    setStatus(kind.save ? QStringLiteral("Saved (version %1)").arg(version) : QString{});
}

void RoiEditorModel::onFailed(const QString& what) {
    if (inFlight_.empty()) {
        return;
    }
    inFlight_.clear();
    setStatus(what);
}

void RoiEditorModel::changed(int row) {
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
    emit roisChanged();
    if (row == selected_) {
        emit selectedRectChanged();
    }
    setDirty(true);
}

void RoiEditorModel::setSelected(int row) {
    if (selected_ == row) {
        return;
    }
    selected_ = row;
    emit selectedChanged();
    emit selectedRectChanged();
}

void RoiEditorModel::setDirty(bool dirty) {
    if (dirty_ == dirty) {
        return;
    }
    dirty_ = dirty;
    emit dirtyChanged();
}

void RoiEditorModel::setStatus(const QString& status) {
    if (status_ == status) {
        return;
    }
    status_ = status;
    emit statusChanged();
}

bool RoiEditorModel::parseConfig(const QByteArray& json, Config& out) {
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const QJsonArray cameras = doc.object().value(QStringLiteral("cameras")).toArray();
    for (const QJsonValue& cameraValue : cameras) {
        const QJsonObject camera = cameraValue.toObject();
        const int cameraId = camera.value(QStringLiteral("camera_id")).toInt(-1);
        if (cameraId < 0) {
            continue;
        }
        auto& list = out[cameraId];
        for (const QJsonValue& roiValue : camera.value(QStringLiteral("rois")).toArray()) {
            const QJsonObject o = roiValue.toObject();
            RoiEntry roi;
            roi.id = o.value(QStringLiteral("id")).toInt(0);
            roi.name = o.value(QStringLiteral("name")).toString();
            roi.x = std::clamp(o.value(QStringLiteral("x")).toDouble(), 0.0, 1.0 - kMin);
            roi.y = std::clamp(o.value(QStringLiteral("y")).toDouble(), 0.0, 1.0 - kMin);
            roi.w = std::clamp(o.value(QStringLiteral("width")).toDouble(), kMin, 1.0 - roi.x);
            roi.h = std::clamp(o.value(QStringLiteral("height")).toDouble(), kMin, 1.0 - roi.y);
            if (roi.id > 0) {
                list.push_back(roi);
            }
        }
    }
    return true;
}

QByteArray RoiEditorModel::toJson() const {
    QJsonArray cameras;
    for (const auto& [cameraId, list] : byCamera_) {
        QJsonArray rois;
        for (const RoiEntry& roi : list) {
            rois.append(QJsonObject{{QStringLiteral("id"), roi.id},
                                    {QStringLiteral("name"), roi.name},
                                    {QStringLiteral("x"), roi.x},
                                    {QStringLiteral("y"), roi.y},
                                    {QStringLiteral("width"), roi.w},
                                    {QStringLiteral("height"), roi.h}});
        }
        cameras.append(
            QJsonObject{{QStringLiteral("camera_id"), cameraId}, {QStringLiteral("rois"), rois}});
    }
    return QJsonDocument{QJsonObject{{QStringLiteral("cameras"), cameras}}}.toJson(
        QJsonDocument::Compact);
}

} // namespace vsort::hmi

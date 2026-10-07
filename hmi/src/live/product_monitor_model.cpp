#include "live/product_monitor_model.hpp"

#include <QVariantMap>
#include <algorithm>
#include <ranges>
#include <utility>

#include <nlohmann/json.hpp>

#include <vsort/common/machine_config.hpp>

namespace vsort::hmi {
namespace {

using namespace std::chrono_literals;

constexpr auto kSnapshotRetry = 1s; // a lost reply is requested again after this time

QString toQString(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

bool consecutive(const QVector<CupRowsModel::Row>& rows) {
    for (qsizetype i = 1; i < rows.size(); ++i) {
        if (rows[i].cupId != rows[0].cupId - i) {
            return false;
        }
    }
    return true;
}

QString cellText(const CupRowData& cup, std::uint16_t sensorId) {
    for (const auto& cell : cup.cells) {
        if (cell.sensorId == sensorId) {
            switch (cell.status) {
            case CupCellStatus::Ok:
                return QStringLiteral("ok");
            case CupCellStatus::NoData:
                return QStringLiteral("nodata");
            case CupCellStatus::Pending:
                break;
            }
            break;
        }
    }
    return QStringLiteral("pending");
}

} // namespace

// ---- CupRowsModel ----

int CupRowsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant CupRowsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
        return {};
    }
    const auto& row = rows_[index.row()];
    switch (role) {
    case CupIdRole:
        return QVariant::fromValue(row.cupId);
    case CellsRole:
        return row.cells;
    default:
        return {};
    }
}

QHash<int, QByteArray> CupRowsModel::roleNames() const {
    return {{CupIdRole, "cupId"}, {CellsRole, "cells"}};
}

void CupRowsModel::reset(const QVector<Row>& rows) {
    beginResetModel();
    rows_ = rows;
    endResetModel();
}

void CupRowsModel::update(const QVector<Row>& rows) {
    if (rows_.isEmpty() || rows.isEmpty() || !consecutive(rows_) || !consecutive(rows) ||
        rows.front().cupId < rows_.front().cupId || rows.back().cupId > rows_.front().cupId) {
        reset(rows);
        return;
    }
    // Cups that left the window at the bottom.
    if (rows.back().cupId > rows_.back().cupId) {
        const auto keep = static_cast<int>(rows_.front().cupId - rows.back().cupId + 1);
        beginRemoveRows({}, keep, static_cast<int>(rows_.size()) - 1);
        rows_.resize(keep);
        endRemoveRows();
    }
    // New cups at the top.
    if (const auto added = static_cast<int>(rows.front().cupId - rows_.front().cupId); added > 0) {
        beginInsertRows({}, 0, added - 1);
        rows_ = rows.mid(0, added) + rows_;
        endInsertRows();
    }
    // Older cups at the bottom (rare: records from before the first cup).
    if (rows.size() > rows_.size()) {
        const auto first = static_cast<int>(rows_.size());
        beginInsertRows({}, first, static_cast<int>(rows.size()) - 1);
        rows_ += rows.mid(first);
        endInsertRows();
    }
    if (rows_.size() != rows.size()) {
        reset(rows);
        return;
    }
    for (qsizetype i = 0; i < rows.size(); ++i) {
        if (rows_[i].cupId != rows[i].cupId) {
            reset(rows);
            return;
        }
        if (rows_[i].cells != rows[i].cells) {
            rows_[i].cells = rows[i].cells;
            const QModelIndex at = index(static_cast<int>(i));
            emit dataChanged(at, at, {CellsRole});
        }
    }
}

// ---- ProductMonitorModel ----

ProductMonitorModel::ProductMonitorModel(ServiceClient& client, QObject* parent)
    : QObject{parent}
    , client_{client} {
    connect(&client_, &ServiceClient::connectedChanged, this,
            &ProductMonitorModel::onConnectedChanged);
    connect(&client_, &ServiceClient::configReceived, this,
            [this](const QString& module, const QByteArray& json, quint32 /*version*/) {
                onConfigReceived(module, json);
            });
    connect(&client_, &ServiceClient::cupSnapshotReceived, this, &ProductMonitorModel::onSnapshot);
    connect(&client_, &ServiceClient::cupUpdateReceived, this, &ProductMonitorModel::onUpdate);
    updateStatus();
}

QVariantList ProductMonitorModel::lanes() const {
    QVariantList out;
    for (const auto& lane : layout_) {
        out.push_back(
            QVariantMap{{QStringLiteral("id"), lane.id}, {QStringLiteral("name"), lane.name}});
    }
    return out;
}

QVariantList ProductMonitorModel::columns() const {
    QVariantList out;
    if (const auto* lane = currentLane(); lane != nullptr) {
        for (const auto& c : lane->columns) {
            out.push_back(QVariantMap{
                {QStringLiteral("sensorId"), c.sensorId},
                {QStringLiteral("key"), c.key},
                {QStringLiteral("title"), c.title},
                {QStringLiteral("kind"),
                 c.key.isEmpty() ? QStringLiteral("photo") : QStringLiteral("measurement")}});
        }
    }
    return out;
}

const ProductMonitorModel::LaneLayout* ProductMonitorModel::currentLane() const {
    if (laneIndex_ < 0 || static_cast<std::size_t>(laneIndex_) >= layout_.size()) {
        return nullptr;
    }
    return &layout_[static_cast<std::size_t>(laneIndex_)];
}

void ProductMonitorModel::setLaneIndex(int index) {
    if (index == laneIndex_ || index < 0 || static_cast<std::size_t>(index) >= layout_.size()) {
        return;
    }
    laneIndex_ = index;
    emit laneIndexChanged();
    emit columnsChanged();
    if (frozen_) {
        frozen_ = false; // another lane: back to live
        emit frozenChanged();
    }
    refresh();
}

void ProductMonitorModel::setFrozen(bool frozen) {
    if (frozen == frozen_) {
        return;
    }
    frozen_ = frozen;
    emit frozenChanged();
    if (!frozen_) {
        refresh();
    }
}

void ProductMonitorModel::onConnectedChanged(bool connected) {
    connected_ = connected;
    emit connectedChanged();
    if (connected) {
        for (auto& [id, store] : stores_) {
            store.synced = false;
        }
        client_.requestConfig(QStringLiteral("machine"));
        requestSnapshot(false);
    }
    updateStatus();
}

void ProductMonitorModel::onConfigReceived(const QString& module, const QByteArray& json) {
    if (module != QStringLiteral("machine")) {
        return;
    }
    configLoaded_ = true;
    const auto parsed =
        nlohmann::json::parse(json.constData(), json.constData() + json.size(), nullptr, false);
    const auto machine = parsed.is_discarded()
                             ? Result<MachineConfig>{makeError(Errc::ParseError, "invalid JSON")}
                             : MachineConfig::fromJson(parsed);
    std::vector<LaneLayout> layout;
    if (machine) {
        configError_.clear();
        for (const auto* lane : machine->lanes()) {
            LaneLayout l{.id = lane->id, .name = toQString(lane->name), .columns = {}};
            for (const auto& s : lane->sensors) {
                if (!s.showInMonitor) {
                    continue;
                }
                l.columns.push_back({.sensorId = s.id, .key = {}, .title = toQString(s.name)});
                for (const auto& m : s.measurements) {
                    QString title = toQString(m.label);
                    if (!m.unit.empty()) {
                        title += QStringLiteral(" [%1]").arg(toQString(m.unit));
                    }
                    l.columns.push_back(
                        {.sensorId = s.id, .key = toQString(m.key), .title = std::move(title)});
                }
            }
            layout.push_back(std::move(l));
        }
    } else {
        configError_ = toQString(machine.error().message);
    }
    layout_ = std::move(layout);
    if (static_cast<std::size_t>(laneIndex_) >= layout_.size()) {
        laneIndex_ = 0;
        emit laneIndexChanged();
    }
    emit layoutChanged();
    emit columnsChanged();
    rows_.update({}); // columns changed: rebuild the rows
    refresh();
    updateStatus();
}

void ProductMonitorModel::requestSnapshot(bool throttled) {
    const auto now = std::chrono::steady_clock::now();
    if (throttled && snapshotPending_ && now - lastSnapshotRequest_ < kSnapshotRetry) {
        return; // one request in flight is enough
    }
    snapshotPending_ = true;
    lastSnapshotRequest_ = now;
    client_.requestCupSnapshot(0);
}

void ProductMonitorModel::apply(LaneStore& store, const QVector<CupRowData>& cups) {
    for (const auto& cup : cups) {
        store.cups[cup.cupId] = cup;
    }
    while (store.cups.size() > store.depth) {
        store.cups.erase(store.cups.begin());
    }
}

void ProductMonitorModel::onSnapshot(const QVector<LaneCupsData>& lanes) {
    snapshotPending_ = false;
    for (const auto& lane : lanes) {
        auto& store = stores_[lane.laneId];
        store.synced = true;
        store.seq = lane.seq;
        store.depth = std::max<std::size_t>(lane.depth, 1);
        store.cups.clear();
        apply(store, lane.cups);
    }
    refresh();
}

void ProductMonitorModel::onUpdate(const LaneCupsData& update) {
    auto& store = stores_[update.laneId];
    if (!store.synced) {
        requestSnapshot(true); // the snapshot will contain this update
        return;
    }
    if (update.seq <= store.seq) {
        return; // already in the snapshot
    }
    if (update.seq > store.seq + 1) {
        requestSnapshot(true); // events were lost (PUB drops for slow subscribers)
    }
    store.seq = update.seq;
    apply(store, update.cups);
    if (const auto* lane = currentLane(); lane != nullptr && lane->id == update.laneId) {
        refresh();
    }
}

void ProductMonitorModel::refresh() {
    if (frozen_) {
        return;
    }
    const auto* lane = currentLane();
    QVector<CupRowsModel::Row> rows;
    if (lane != nullptr) {
        if (const auto it = stores_.find(lane->id); it != stores_.end()) {
            for (const auto& [cupId, cup] : std::ranges::reverse_view(it->second.cups)) {
                CupRowsModel::Row row{.cupId = cupId, .cells = {}};
                for (const auto& column : lane->columns) {
                    row.cells.push_back(column.key.isEmpty()
                                            ? cellText(cup, column.sensorId)
                                            : QStringLiteral("empty")); // values come with P60
                }
                rows.push_back(std::move(row));
            }
        }
    }
    rows_.update(rows);
}

void ProductMonitorModel::updateStatus() {
    QString status;
    if (!connected_) {
        status = QStringLiteral("Service offline");
    } else if (!configLoaded_) {
        status = QStringLiteral("Loading the machine config…");
    } else if (!configError_.isEmpty()) {
        status = QStringLiteral("Machine config invalid: %1").arg(configError_);
    } else if (layout_.empty()) {
        status = QStringLiteral("No lanes in the machine config");
    }
    if (status != status_) {
        status_ = std::move(status);
        emit statusChanged();
    }
}

} // namespace vsort::hmi

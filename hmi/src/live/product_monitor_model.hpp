#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <chrono>
#include <cstdint>
#include <map>
#include <vector>

#include <vsort/common/machine_config.hpp>

#include "live/cup_data.hpp"
#include "live/service_client.hpp"

namespace vsort::hmi {

// Rows of the G140.10 cup table: newest cup first. Updated in place (insert at the top, remove at
// the bottom, change in between) so the view keeps its delegates and scroll position.
class CupRowsModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role { CupIdRole = Qt::UserRole + 1, CellsRole, ValuesRole };

    // One entry per column in `cells` and `values`. Photo columns: "ok", "nodata", "pending".
    // Measurement columns: "value", "present", "absent" (with the text in `values`), or "empty"
    // (no value: no photo yet, no data, or not measured).
    struct Row {
        qint64 cupId{0};
        QStringList cells;
        QStringList values{};

        bool operator==(const Row&) const = default;
    };

    using QAbstractListModel::QAbstractListModel;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // `rows`: newest first, consecutive cup IDs.
    void update(const QVector<Row>& rows);
    // Plain update by index for a fixed window: changed rows emit dataChanged, the row count only
    // changes at the end. Nothing is inserted at the top, so a view never shifts its content.
    void assign(const QVector<Row>& rows);
    [[nodiscard]] const QVector<Row>& rows() const noexcept { return rows_; }

private:
    void reset(const QVector<Row>& rows);

    QVector<Row> rows_;
};

// Model behind G140.10 Product monitor (P80.110). Lanes and columns come from the "machine"
// config (sensors with show_in_monitor; per sensor a photo column plus its measurements), the
// cups from GetCupSnapshot and the CupUpdate events (MSG-50-02).
// Sync: on connect the snapshot is requested; updates with a seq not above the lane's seq are
// dropped; a gap in seq requests a new snapshot. Frozen: the table stops changing while the data
// keeps arriving; back to live shows the current state. Switching the lane goes back to live.
class ProductMonitorModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList lanes READ lanes NOTIFY layoutChanged)
    Q_PROPERTY(int laneIndex READ laneIndex WRITE setLaneIndex NOTIFY laneIndexChanged)
    Q_PROPERTY(QVariantList columns READ columns NOTIFY columnsChanged)
    Q_PROPERTY(QObject* rows READ rows CONSTANT)
    Q_PROPERTY(QObject* windowRows READ windowRows CONSTANT)
    Q_PROPERTY(int rowCount READ rowCount NOTIFY rowCountChanged)
    Q_PROPERTY(int firstRow READ firstRow WRITE setFirstRow NOTIFY firstRowChanged)
    Q_PROPERTY(int windowSize READ windowSize WRITE setWindowSize NOTIFY windowSizeChanged)
    Q_PROPERTY(bool frozen READ frozen WRITE setFrozen NOTIFY frozenChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    // `client` must outlive the model.
    explicit ProductMonitorModel(ServiceClient& client, QObject* parent = nullptr);
    ~ProductMonitorModel() override = default;
    ProductMonitorModel(const ProductMonitorModel&) = delete;
    ProductMonitorModel& operator=(const ProductMonitorModel&) = delete;
    ProductMonitorModel(ProductMonitorModel&&) = delete;
    ProductMonitorModel& operator=(ProductMonitorModel&&) = delete;

    // [{id, name}]
    [[nodiscard]] QVariantList lanes() const;
    [[nodiscard]] int laneIndex() const noexcept { return laneIndex_; }
    void setLaneIndex(int index);
    // [{sensorId, key, title, kind}] for the current lane; kind "photo" or "measurement".
    [[nodiscard]] QVariantList columns() const;
    [[nodiscard]] QObject* rows() noexcept { return &rows_; }
    [[nodiscard]] const CupRowsModel& rowsModel() const noexcept { return rows_; }
    // What the page shows: rows firstRow .. firstRow + windowSize - 1 of `rows`, by index.
    [[nodiscard]] QObject* windowRows() noexcept { return &window_; }
    [[nodiscard]] const CupRowsModel& windowModel() const noexcept { return window_; }
    [[nodiscard]] int rowCount() const noexcept { return rowCount_; }
    [[nodiscard]] int firstRow() const noexcept { return firstRow_; }
    void setFirstRow(int row); // clamped to 0 .. rowCount - windowSize
    [[nodiscard]] int windowSize() const noexcept { return windowSize_; }
    void setWindowSize(int size); // at least 1
    [[nodiscard]] bool frozen() const noexcept { return frozen_; }
    void setFrozen(bool frozen);
    [[nodiscard]] bool connected() const noexcept { return connected_; }
    // Empty when everything is fine; otherwise what the page shows instead of the table.
    [[nodiscard]] QString status() const { return status_; }

    Q_INVOKABLE void toggleFreeze() { setFrozen(!frozen_); }

signals:
    void layoutChanged();
    void laneIndexChanged();
    void columnsChanged();
    void frozenChanged();
    void connectedChanged();
    void statusChanged();
    void rowCountChanged();
    void firstRowChanged();
    void windowSizeChanged();

private:
    struct Column {
        std::uint16_t sensorId{0};
        QString key; // empty: the photo column
        QString title;
        MeasurementFormat format{MeasurementFormat::Number};
    };
    struct LaneLayout {
        std::uint16_t id{0};
        QString name;
        std::vector<Column> columns;
    };
    struct LaneStore {
        bool synced{false};
        std::uint64_t seq{0};
        std::size_t depth{150};
        std::map<std::int64_t, CupRowData> cups;
    };

    void onConnectedChanged(bool connected);
    void onConfigReceived(const QString& module, const QByteArray& json);
    void onSnapshot(const QVector<LaneCupsData>& lanes);
    void onUpdate(const LaneCupsData& update);
    static void apply(LaneStore& store, const QVector<CupRowData>& cups);
    void requestSnapshot(bool throttled);
    void refresh();
    void updateWindow();
    void updateStatus();
    [[nodiscard]] const LaneLayout* currentLane() const;

    ServiceClient& client_;
    CupRowsModel rows_;
    CupRowsModel window_;
    std::vector<LaneLayout> layout_;
    std::map<std::uint16_t, LaneStore> stores_;
    int laneIndex_{0};
    int firstRow_{0};
    int windowSize_{40};
    int rowCount_{0};
    bool frozen_{false};
    bool connected_{false};
    bool configLoaded_{false};
    QString configError_;
    QString status_;
    bool snapshotPending_{false};
    std::chrono::steady_clock::time_point lastSnapshotRequest_;
};

} // namespace vsort::hmi

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "fake_service.hpp"
#include "live/product_monitor_model.hpp"
#include "live/service_client.hpp"
#include "test_util.hpp"

namespace {

using namespace vsort::hmi;
using namespace vsort::hmi::test;

// Lane 1: Camera 1 (size and a presence column), Camera 2, Camera 3 hidden. Lane 2: Camera 4.
const std::string kMachine = R"({"lines": [{"id": 1, "name": "Line 1", "lanes": [
  {"id": 1, "name": "Lane 1", "sensors": [
    {"id": 1, "name": "Camera 1", "kind": "camera", "camera_id": 0, "roi_id": 0,
     "offset_cups": 0, "show_in_monitor": true, "measurements": [
       {"key": "size_mm", "label": "Size", "unit": "mm"},
       {"key": "present", "label": "Cup", "unit": "", "format": "presence"}]},
    {"id": 2, "name": "Camera 2", "kind": "camera", "camera_id": 1, "roi_id": 0,
     "offset_cups": 5, "show_in_monitor": true, "measurements": []},
    {"id": 3, "name": "Camera 3", "kind": "camera", "camera_id": 2, "roi_id": 0,
     "offset_cups": 10, "show_in_monitor": false, "measurements": []}]},
  {"id": 2, "name": "Lane 2", "sensors": [
    {"id": 4, "name": "Camera 4", "kind": "camera", "camera_id": 3, "roi_id": 0,
     "offset_cups": 0, "show_in_monitor": true, "measurements": []}]}]}]})";

CupRowData cup(std::int64_t id, CupCellStatus s1, CupCellStatus s2,
               CupCellStatus s3 = CupCellStatus::Pending) {
    return {.cupId = id,
            .cells = {{.sensorId = 1, .status = s1},
                      {.sensorId = 2, .status = s2},
                      {.sensorId = 3, .status = s3}}};
}

class ProductMonitorModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        service.setConfigJson("machine", kMachine);
        service.setCupSnapshot(
            {LaneCupsData{.laneId = 1,
                          .seq = 5,
                          .depth = 4,
                          .cups = {cup(11, CupCellStatus::Ok, CupCellStatus::Pending),
                                   cup(10, CupCellStatus::Ok, CupCellStatus::NoData)}}});
        client.start();
        ASSERT_TRUE(spinUntil([&] { return model.rowsModel().rowCount() == 2; },
                              [this] { service.pump(); }));
    }

    [[nodiscard]] QStringList cells(int row) const {
        return model.rowsModel().rows().at(row).cells;
    }
    [[nodiscard]] qint64 cupId(int row) const { return model.rowsModel().rows().at(row).cupId; }

    void sendUpdate(std::uint64_t seq, QVector<CupRowData> cups) {
        service.publishCupUpdate({.laneId = 1, .seq = seq, .depth = 0, .cups = std::move(cups)});
        spinFor(std::chrono::milliseconds{100}, [this] { service.pump(); });
    }

    FakeService service{{}};
    ServiceClient client{{.host = QStringLiteral("127.0.0.1"),
                          .commandPort = FakeService::kCommandPort,
                          .eventPort = FakeService::kEventPort}};
    ProductMonitorModel model{client};
};

} // namespace

TEST_F(ProductMonitorModelTest, LanesAndColumnsComeFromTheMachineConfig) {
    EXPECT_TRUE(model.connected());
    EXPECT_TRUE(model.status().isEmpty()) << model.status().toStdString();
    const auto lanes = model.lanes();
    ASSERT_EQ(lanes.size(), 2);
    EXPECT_EQ(lanes[1].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Lane 2"));

    const auto columns = model.columns();
    ASSERT_EQ(columns.size(), 4); // Camera 1, Size [mm], Cup, Camera 2 (Camera 3 hidden)
    EXPECT_EQ(columns[0].toMap().value(QStringLiteral("title")).toString(),
              QStringLiteral("Camera 1"));
    EXPECT_EQ(columns[0].toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("photo"));
    EXPECT_EQ(columns[1].toMap().value(QStringLiteral("title")).toString(),
              QStringLiteral("Size [mm]"));
    EXPECT_EQ(columns[1].toMap().value(QStringLiteral("kind")).toString(),
              QStringLiteral("measurement"));
    EXPECT_EQ(columns[2].toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Cup"));
    EXPECT_EQ(columns[3].toMap().value(QStringLiteral("sensorId")).toInt(), 2);
}

TEST_F(ProductMonitorModelTest, SnapshotRowsNewestFirst) {
    EXPECT_EQ(cupId(0), 11);
    EXPECT_EQ(cupId(1), 10);
    EXPECT_EQ(cells(0), (QStringList{"ok", "empty", "empty", "pending"}));
    EXPECT_EQ(cells(1), (QStringList{"ok", "empty", "empty", "nodata"}));
}

TEST_F(ProductMonitorModelTest, UpdatesInsertChangeAndTrim) {
    sendUpdate(6, {cup(13, CupCellStatus::Ok, CupCellStatus::Pending),
                   cup(12, CupCellStatus::NoData, CupCellStatus::Pending),
                   cup(11, CupCellStatus::Ok, CupCellStatus::Ok)});
    ASSERT_EQ(model.rowsModel().rowCount(), 4);
    EXPECT_EQ(cupId(0), 13);
    EXPECT_EQ(cells(1)[0], QStringLiteral("nodata"));
    EXPECT_EQ(cells(2)[3], QStringLiteral("ok")); // cup 11, Camera 2 now has its photo

    sendUpdate(7, {cup(14, CupCellStatus::Ok, CupCellStatus::Pending)});
    ASSERT_EQ(model.rowsModel().rowCount(), 4); // depth 4: cup 10 left the table
    EXPECT_EQ(cupId(0), 14);
    EXPECT_EQ(cupId(3), 11);
}

TEST_F(ProductMonitorModelTest, OldUpdatesAreIgnoredAndGapsResync) {
    sendUpdate(5, {cup(11, CupCellStatus::NoData, CupCellStatus::NoData)}); // already included
    EXPECT_EQ(cells(0)[0], QStringLiteral("ok"));

    const int before = service.cupSnapshotRequests();
    service.setCupSnapshot(
        {LaneCupsData{// what the service has after the lost events
                      .laneId = 1,
                      .seq = 9,
                      .depth = 4,
                      .cups = {cup(12, CupCellStatus::Ok, CupCellStatus::Pending),
                               cup(11, CupCellStatus::Ok, CupCellStatus::Ok),
                               cup(10, CupCellStatus::Ok, CupCellStatus::NoData)}}});
    sendUpdate(9, {cup(12, CupCellStatus::Ok, CupCellStatus::Pending)}); // 6..8 lost
    EXPECT_EQ(service.cupSnapshotRequests(), before + 1);
    ASSERT_EQ(model.rowsModel().rowCount(), 3);
    EXPECT_EQ(cupId(0), 12);
    EXPECT_EQ(cells(1)[3], QStringLiteral("ok")); // from the new snapshot
}

TEST_F(ProductMonitorModelTest, FrozenTableStopsChangingUntilBackToLive) {
    model.toggleFreeze();
    EXPECT_TRUE(model.frozen());
    sendUpdate(6, {cup(12, CupCellStatus::Ok, CupCellStatus::Pending)});
    EXPECT_EQ(model.rowsModel().rowCount(), 2);
    EXPECT_EQ(cupId(0), 11);

    model.toggleFreeze();
    EXPECT_FALSE(model.frozen());
    ASSERT_EQ(model.rowsModel().rowCount(), 3);
    EXPECT_EQ(cupId(0), 12);
}

TEST_F(ProductMonitorModelTest, OtherLaneHasItsOwnColumnsAndUnfreezes) {
    model.setFrozen(true);
    model.setLaneIndex(1);
    EXPECT_FALSE(model.frozen());
    EXPECT_EQ(model.columns().size(), 1);
    EXPECT_EQ(model.rowsModel().rowCount(), 0); // no cups for lane 2 yet
    model.setLaneIndex(0);
    EXPECT_EQ(model.rowsModel().rowCount(), 2);
}

TEST(CupRowsModel, InPlaceUpdatesKeepRows) {
    CupRowsModel rows;
    rows.update({{.cupId = 5, .cells = {"ok"}}, {.cupId = 4, .cells = {"pending"}}});
    int inserted = 0;
    int removed = 0;
    int changed = 0;
    int resets = 0;
    QObject::connect(&rows, &QAbstractItemModel::rowsInserted, [&] { ++inserted; });
    QObject::connect(&rows, &QAbstractItemModel::rowsRemoved, [&] { ++removed; });
    QObject::connect(&rows, &QAbstractItemModel::dataChanged, [&] { ++changed; });
    QObject::connect(&rows, &QAbstractItemModel::modelReset, [&] { ++resets; });

    rows.update({{.cupId = 6, .cells = {"ok"}}, {.cupId = 5, .cells = {"nodata"}}});
    EXPECT_EQ(inserted, 1);
    EXPECT_EQ(removed, 1);
    EXPECT_EQ(changed, 1);
    EXPECT_EQ(resets, 0);
    ASSERT_EQ(rows.rowCount(), 2);
    EXPECT_EQ(rows.rows()[0].cupId, 6);

    rows.update({{.cupId = 20, .cells = {"ok"}}}); // no overlap: reset
    EXPECT_EQ(resets, 1);
    EXPECT_EQ(rows.rowCount(), 1);
}

TEST_F(ProductMonitorModelTest, MeasurementsShowInTheirColumns) {
    auto measured = cup(12, CupCellStatus::Ok, CupCellStatus::Pending);
    measured.cells[0].measurements = {{.key = QStringLiteral("size_mm"), .value = 41.27},
                                      {.key = QStringLiteral("present"), .value = 1.0}};
    auto emptyCup = cup(13, CupCellStatus::Ok, CupCellStatus::Pending);
    emptyCup.cells[0].measurements = {{.key = QStringLiteral("present"), .value = 0.0}};
    sendUpdate(6, {emptyCup, measured});
    ASSERT_EQ(model.rowsModel().rowCount(), 4);
    const auto& rows = model.rowsModel().rows();
    EXPECT_EQ(rows[0].cells, (QStringList{"ok", "empty", "absent", "pending"})); // cup 13
    EXPECT_EQ(rows[0].values[2], QStringLiteral("empty"));
    EXPECT_EQ(rows[1].cells, (QStringList{"ok", "value", "present", "pending"})); // cup 12
    EXPECT_EQ(rows[1].values[1], QStringLiteral("41.3"));
    EXPECT_EQ(rows[1].values[2], QStringLiteral("present"));
    EXPECT_EQ(rows[1].values[0], QString{}); // photo column: no text

    auto lost = measured; // the photo became NoData: its values are not shown
    lost.cells[0].status = CupCellStatus::NoData;
    sendUpdate(7, {lost});
    EXPECT_EQ(cells(1), (QStringList{"nodata", "empty", "empty", "pending"}));
}

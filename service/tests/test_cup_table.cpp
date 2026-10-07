#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "cups/cup_table.hpp"

using namespace vsort;
using namespace vsort::service;

namespace {

// V1 layout: one lane, sensors 1..4 at offsets 0, 5, 10, 25.
MachineConfig machine(bool twoLanes = false) {
    auto config = machineDefaults();
    auto& sensors = config["lines"][0]["lanes"][0]["sensors"];
    const std::array<int, 4> offsets{0, 5, 10, 25};
    for (std::size_t i = 0; i < 4; ++i) {
        sensors[i]["offset_cups"] = offsets[i];
    }
    if (twoLanes) {
        auto lane2 = config["lines"][0]["lanes"][0];
        lane2["id"] = 2;
        lane2["name"] = "Lane 2";
        for (std::size_t i = 0; i < 4; ++i) {
            lane2["sensors"][i]["id"] = 11 + i;
        }
        config["lines"][0]["lanes"].push_back(lane2);
    }
    return MachineConfig::fromJson(config).value();
}

ObjectRecord rec(std::int64_t cupId, std::uint16_t sensorId, PhotoStatus status = PhotoStatus::Ok,
                 std::uint16_t laneId = 1) {
    ObjectRecord r;
    r.laneId = laneId;
    r.cupId = cupId;
    r.sensorId = sensorId;
    r.status = status;
    return r;
}

CellStatus cell(const CupTable& t, std::int64_t cupId, std::size_t sensor, std::uint16_t lane = 1) {
    const auto snap = t.snapshot(lane); // keep the temporary alive for the loop
    for (const auto& c : snap.at(0).cups) {
        if (c.cupId == cupId) {
            return c.cells.at(sensor).status;
        }
    }
    ADD_FAILURE() << "cup " << cupId << " not in the table";
    return CellStatus::Pending;
}

} // namespace

TEST(CupTable, RecordsFillTheCells) {
    CupTable t{machine()};
    EXPECT_TRUE(t.apply(rec(0, 1)));
    EXPECT_TRUE(t.apply(rec(1, 1)));
    EXPECT_TRUE(t.apply(rec(1, 2, PhotoStatus::NoData)));
    const auto snap = t.snapshot(std::nullopt);
    ASSERT_EQ(snap.size(), 1U);
    EXPECT_EQ(snap[0].laneId, 1);
    EXPECT_EQ(snap[0].depth, 50);
    ASSERT_EQ(snap[0].cups.size(), 2U);
    EXPECT_EQ(snap[0].cups[0].cupId, 1); // newest first
    ASSERT_EQ(snap[0].cups[0].cells.size(), 4U);
    EXPECT_EQ(snap[0].cups[0].cells[0].sensorId, 1);
    EXPECT_EQ(snap[0].cups[0].cells[0].status, CellStatus::Ok);
    EXPECT_EQ(snap[0].cups[0].cells[1].status, CellStatus::NoData);
    EXPECT_EQ(snap[0].cups[0].cells[3].status, CellStatus::Pending);
}

TEST(CupTable, LaterRecordReplacesEarlierOne) {
    CupTable t{machine()};
    t.apply(rec(3, 1));
    t.apply(rec(3, 1, PhotoStatus::NoData)); // correction by the cross-sensor check
    EXPECT_EQ(cell(t, 3, 0), CellStatus::NoData);
    t.apply(rec(3, 1));
    EXPECT_EQ(cell(t, 3, 0), CellStatus::Ok);
}

TEST(CupTable, KeepsTheLastFiftyCupsWithoutGaps) {
    CupTable t{machine()};
    for (std::int64_t id = 0; id < 60; ++id) {
        t.apply(rec(id, 1));
    }
    t.apply(rec(70, 1)); // 60..69 never reported by sensor 1: created as rows
    const auto cups = t.snapshot(1).at(0).cups;
    ASSERT_EQ(cups.size(), 50U);
    EXPECT_EQ(cups.front().cupId, 70);
    EXPECT_EQ(cups.back().cupId, 21);
    for (std::size_t i = 1; i < cups.size(); ++i) {
        EXPECT_EQ(cups[i - 1].cupId - cups[i].cupId, 1);
    }
    EXPECT_FALSE(t.apply(rec(20, 2))); // older than the window
    EXPECT_EQ(t.ignored(), 1U);
}

TEST(CupTable, PendingCellsBecomeNoDataWhenTheCupHasPassed) {
    CupTable t{machine()};
    for (std::int64_t id = 0; id <= 20; ++id) {
        t.apply(rec(id, 1)); // only sensor 1 reports (sensor 2 offline)
    }
    // Sensor 2 (offset 5, margin 3): cups more than 8 behind the newest (20) are NoData.
    EXPECT_EQ(cell(t, 11, 1), CellStatus::NoData);
    EXPECT_EQ(cell(t, 12, 1), CellStatus::Pending);
    // Sensor 4 (offset 25): nothing is due yet.
    EXPECT_EQ(cell(t, 0, 3), CellStatus::Pending);
}

TEST(CupTable, UpdatesCarryChangedCupsOnly) {
    CupTable t{machine()};
    EXPECT_TRUE(t.takeUpdates().empty());
    t.apply(rec(0, 1));
    t.apply(rec(1, 1));
    auto updates = t.takeUpdates();
    ASSERT_EQ(updates.size(), 1U);
    EXPECT_EQ(updates[0].seq, 1U);
    ASSERT_EQ(updates[0].cups.size(), 2U);
    EXPECT_EQ(updates[0].cups[0].cupId, 1);
    EXPECT_TRUE(t.takeUpdates().empty());

    t.apply(rec(0, 2));
    t.apply(rec(0, 2)); // same state again: still one change
    updates = t.takeUpdates();
    ASSERT_EQ(updates.size(), 1U);
    EXPECT_EQ(updates[0].seq, 2U);
    ASSERT_EQ(updates[0].cups.size(), 1U);
    EXPECT_EQ(updates[0].cups[0].cupId, 0);
    EXPECT_EQ(t.snapshot(1).at(0).seq, 2U);
}

TEST(CupTable, DownstreamRecordsBeforeTheFirstCup) {
    CupTable t{machine()};
    t.apply(rec(5, 1));
    t.apply(rec(-2, 4)); // passed sensor 1 before tracking started
    const auto cups = t.snapshot(1).at(0).cups;
    ASSERT_EQ(cups.size(), 8U); // -2..5, continuous
    EXPECT_EQ(cups.back().cupId, -2);
    EXPECT_EQ(cups.back().cells[3].status, CellStatus::Ok);
}

TEST(CupTable, LanesAreSeparate) {
    CupTable t{machine(true)};
    t.apply(rec(4, 11, PhotoStatus::Ok, 2));
    EXPECT_FALSE(t.apply(rec(4, 11, PhotoStatus::Ok, 1))); // sensor 11 is not in lane 1
    EXPECT_FALSE(t.apply(rec(4, 1, PhotoStatus::Ok, 9)));  // no lane 9
    EXPECT_EQ(t.snapshot(std::nullopt).size(), 2U);
    EXPECT_TRUE(t.snapshot(1).at(0).cups.empty());
    EXPECT_EQ(t.snapshot(2).at(0).cups.size(), 1U);
    EXPECT_TRUE(t.snapshot(7).empty());
    const auto updates = t.takeUpdates();
    ASSERT_EQ(updates.size(), 1U);
    EXPECT_EQ(updates[0].laneId, 2);
}

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "tracking/frame_sequence_tracker.hpp"

using namespace vsort;
using namespace vsort::service;

namespace {

// V1 layout: one lane, cameras 0..3 at offsets 0, 3, 6, 9.
std::vector<TrackedSensor> oneLane() {
    std::vector<TrackedSensor> out;
    for (std::uint16_t i = 0; i < 4; ++i) {
        out.push_back({.laneId = 1,
                       .sensorId = static_cast<std::uint16_t>(i + 1),
                       .cameraId = i,
                       .offsetCups = 3U * i,
                       .roi = {}});
    }
    return out;
}

camera::FrameMetadata meta(std::uint16_t camera, std::uint64_t frameId) {
    camera::FrameMetadata m;
    m.cameraIndex = camera;
    m.frameId = FrameId{frameId};
    m.hostTimestamp = Timestamp{std::chrono::milliseconds{100 * frameId}};
    m.deviceTimestampNs = 1000 * frameId;
    m.width = 1000;
    m.height = 500;
    return m;
}

std::vector<ObjectRecord> feed(FrameSequenceTracker& t, std::uint16_t camera,
                               std::uint64_t frameId) {
    std::vector<ObjectRecord> out;
    t.onFrame(meta(camera, frameId), out);
    return out;
}

} // namespace

TEST(FrameSequenceTracker, OneFrameIsOneCup) {
    FrameSequenceTracker t{oneLane()};
    for (std::uint64_t f = 50; f < 60; ++f) { // first frame ID is arbitrary
        for (std::uint16_t cam = 0; cam < 4; ++cam) {
            const auto out = feed(t, cam, f);
            ASSERT_EQ(out.size(), 1U);
            const auto& r = out[0];
            EXPECT_EQ(r.status, PhotoStatus::Ok);
            EXPECT_EQ(r.laneId, 1);
            EXPECT_EQ(r.sensorId, cam + 1);
            EXPECT_EQ(r.sensorCount, f - 50);
            EXPECT_EQ(r.cupId, static_cast<std::int64_t>(f - 50) - (3 * cam));
            EXPECT_EQ(r.frameId, FrameId{f});
            EXPECT_EQ(r.deviceTimestampNs, 1000 * f);
            EXPECT_EQ(r.crop.width, 1000U);
        }
    }
    for (const auto& c : t.counters()) {
        EXPECT_EQ(c.count, 10U);
        EXPECT_EQ(c.noData, 0U);
    }
}

TEST(FrameSequenceTracker, FrameIdGapGivesNoDataCups) {
    FrameSequenceTracker t{oneLane()};
    ASSERT_EQ(feed(t, 2, 10).size(), 1U);
    const auto out = feed(t, 2, 13); // frames 11 and 12 lost after exposure
    ASSERT_EQ(out.size(), 3U);
    EXPECT_EQ(out[0].status, PhotoStatus::NoData);
    EXPECT_EQ(out[0].cupId, 1 - 6);
    EXPECT_EQ(out[1].status, PhotoStatus::NoData);
    EXPECT_EQ(out[1].cupId, 2 - 6);
    EXPECT_EQ(out[2].status, PhotoStatus::Ok);
    EXPECT_EQ(out[2].cupId, 3 - 6);
    EXPECT_EQ(out[2].frameId, FrameId{13});
    EXPECT_EQ(t.counters()[2].noData, 2U);
    EXPECT_EQ(t.counters()[2].count, 4U);
}

TEST(FrameSequenceTracker, ResetAndHugeGapDoNotFlood) {
    FrameSequenceTracker t{oneLane(), {.maxGapFill = 5}};
    ASSERT_EQ(feed(t, 0, 100).size(), 1U);
    ASSERT_EQ(feed(t, 0, 3).size(), 1U);    // camera restarted its counter
    ASSERT_EQ(feed(t, 0, 1000).size(), 1U); // jump larger than maxGapFill
    ASSERT_EQ(feed(t, 0, 1003).size(), 3U); // normal gap again
    const auto c = t.counters()[0];
    EXPECT_EQ(c.idResets, 2U);
    EXPECT_EQ(c.count, 6U);
    EXPECT_EQ(c.noData, 2U);
}

TEST(FrameSequenceTracker, CameraServingTwoLanes) {
    auto sensors = oneLane();
    sensors.push_back({.laneId = 2, .sensorId = 11, .cameraId = 0, .offsetCups = 0, .roi = {}});
    sensors[0].roi = {.x = 0.0, .y = 0.0, .width = 0.5, .height = 1.0};
    sensors[4].roi = {.x = 0.5, .y = 0.0, .width = 0.5, .height = 1.0};
    FrameSequenceTracker t{sensors};
    const auto out = feed(t, 0, 1);
    ASSERT_EQ(out.size(), 2U);
    EXPECT_EQ(out[0].laneId, 1);
    EXPECT_EQ(out[0].crop.x, 0U);
    EXPECT_EQ(out[1].laneId, 2);
    EXPECT_EQ(out[1].crop.x, 500U);
    EXPECT_EQ(out[1].crop.width, 500U);
}

TEST(FrameSequenceTracker, IgnoresUnmappedCameras) {
    FrameSequenceTracker t{oneLane()};
    EXPECT_TRUE(feed(t, 9, 1).empty());
    EXPECT_EQ(t.unmappedFrames(), 1U);
}

TEST(FrameSequenceTracker, BuildsSensorsFromMachineConfig) {
    auto config = machineDefaults();
    auto& sensors = config["lines"][0]["lanes"][0]["sensors"];
    sensors[1]["offset_cups"] = 4;
    sensors[1]["roi_id"] = 1;
    sensors[2]["offset_cups"] = 4;
    sensors[2]["roi_id"] = 8; // not drawn: whole image
    sensors[3]["offset_cups"] = 9;
    const auto machine = MachineConfig::fromJson(config);
    ASSERT_TRUE(machine.has_value()) << machine.error().what();
    const auto rois = nlohmann::json::parse(R"({"cameras": [{"camera_id": 1, "rois": [
      {"id": 1, "name": "L1", "x": 0.25, "y": 0.0, "width": 0.5, "height": 1.0}]}]})");
    const auto tracked = makeTrackedSensors(*machine, rois);
    ASSERT_EQ(tracked.size(), 4U);
    EXPECT_EQ(tracked[1].offsetCups, 4U);
    EXPECT_DOUBLE_EQ(tracked[1].roi.x, 0.25);
    EXPECT_DOUBLE_EQ(tracked[2].roi.width, 1.0);
    EXPECT_EQ(tracked[3].cameraId, 3);
}

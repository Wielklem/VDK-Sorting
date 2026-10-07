#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include <vsort/common/machine_config.hpp>
#include <vsort/common/roi_config.hpp>

#include "test_ipc_util.hpp"
#include "tracking/frame_sequence_tracker.hpp"
#include "tracking/tracking_config.hpp"
#include "tracking/tracking_module.hpp"

using namespace vsort;
using namespace vsort::service;
using namespace std::chrono_literals;

TEST(FrameInbox, IgnoresWhileClosedAndCountsDropsWhenFull) {
    FrameInbox inbox{2};
    camera::FrameMetadata m;
    EXPECT_FALSE(inbox.push(m)); // closed: ignored, not a drop
    EXPECT_EQ(inbox.dropped(), 0U);
    inbox.open();
    EXPECT_TRUE(inbox.push(m));
    EXPECT_TRUE(inbox.push(m));
    EXPECT_FALSE(inbox.push(m)); // full
    EXPECT_EQ(inbox.dropped(), 1U);
    EXPECT_EQ(inbox.accepted(), 2U);
    EXPECT_TRUE(inbox.pop().has_value());
    EXPECT_TRUE(inbox.push(m));
    inbox.close();
    EXPECT_FALSE(inbox.push(m));
    EXPECT_EQ(inbox.dropped(), 1U);
}

TEST(TrackingModule, PublishesObjectRecords) {
    std::vector<TrackedSensor> sensors{
        {.laneId = 1, .sensorId = 1, .cameraId = 0, .offsetCups = 0, .roi = {}},
        {.laneId = 1, .sensorId = 2, .cameraId = 1, .offsetCups = 2, .roi = {}}};
    TrackingModule module{std::make_unique<FrameSequenceTracker>(sensors)};
    EXPECT_EQ(module.name(), "tracking");
    EXPECT_EQ(module.dependencies(), std::vector<std::string>{"camera"});

    MessageBus bus;
    auto records = bus.subscribe<ObjectRecord>();
    ModuleContext context{bus};
    ASSERT_TRUE(module.init(context).has_value());

    const auto early = testutil::makeFrame(0, 1, 64, 32, 0);
    module.submit(early->frame); // before start: ignored
    ASSERT_TRUE(module.start().has_value());

    for (std::uint64_t f = 10; f < 15; ++f) {
        if (f != 12) { // frame 12 of camera 0 is lost
            module.submit(testutil::makeFrame(0, f, 64, 32, 0)->frame);
        }
        module.submit(testutil::makeFrame(1, f, 64, 32, 0)->frame);
    }
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (module.recordsPublished() < 10 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    module.stop();
    EXPECT_EQ(module.framesProcessed(), 9U);
    EXPECT_EQ(module.framesDropped(), 0U);
    ASSERT_EQ(module.recordsPublished(), 10U); // camera 0: 4 + 1 NoData, camera 1: 5

    std::vector<ObjectRecord> cam0;
    std::vector<ObjectRecord> cam1;
    records->drain([&](const ObjectRecord& r) { (r.cameraId == 0 ? cam0 : cam1).push_back(r); });
    ASSERT_EQ(cam0.size(), 5U);
    ASSERT_EQ(cam1.size(), 5U);
    EXPECT_EQ(cam0[2].status, PhotoStatus::NoData);
    EXPECT_EQ(cam0[2].cupId, 2);
    EXPECT_EQ(cam0[4].frameId, FrameId{14});
    EXPECT_EQ(cam1[0].cupId, -2);
    EXPECT_EQ(cam1[4].cupId, 2);
    EXPECT_EQ(module.health().state, HealthState::Ok);
}

TEST(TrackingModule, BuildsTrackerFromConfig) {
    const auto dir = std::filesystem::temp_directory_path() / "vsort_tracking_module_test";
    std::filesystem::remove_all(dir);
    {
        MessageBus bus;
        FileConfigStore store{dir, bus};
        ASSERT_TRUE(registerRoiConfig(store).has_value());
        ASSERT_TRUE(registerMachineConfig(store).has_value());
        TrackingModule module{&store};
        ModuleContext context{bus};
        ASSERT_TRUE(module.init(context).has_value());
        ASSERT_TRUE(module.start().has_value());
        module.stop();
    }
    std::filesystem::remove_all(dir);
}

TEST(TrackingModule, InitFailsWithoutConfig) {
    MessageBus bus;
    ModuleContext context{bus};
    TrackingModule module{static_cast<const IConfigStore*>(nullptr)};
    EXPECT_FALSE(module.init(context).has_value());
    module.stop(); // safe after a failed init
}

TEST(TrackingConfig, DefaultsRoundTrip) {
    EXPECT_TRUE(validateJson(trackingSchema(), trackingDefaults()).has_value());
    auto json = trackingDefaults();
    json["miss_factor"] = 1.8;
    json["mark_limit"] = 20;
    const auto options = trackingOptionsFromJson(json);
    ASSERT_TRUE(options.has_value()) << options.error().what();
    EXPECT_DOUBLE_EQ(options->timing.missFactor, 1.8);
    EXPECT_EQ(options->markLimit, 20U);
    EXPECT_EQ(trackingOptionsToJson(*options), json);
    json["miss_factor"] = 0.9; // below the schema minimum
    EXPECT_FALSE(trackingOptionsFromJson(json).has_value());
}

TEST(TrackingModule, KeepsLearnedPhasesInTheStateFile) {
    const auto dir = std::filesystem::temp_directory_path() / "vsort_tracking_state_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto file = dir / "tracking_state.json";
    const auto saved = nlohmann::json::parse(
        R"({"lanes": [{"lane_id": 1, "sensors": [{"sensor_id": 2, "phase": 0.25}]}]})");
    {
        std::ofstream out{file};
        out << saved.dump();
    }
    {
        std::vector<TrackedSensor> sensors{
            {.laneId = 1, .sensorId = 1, .cameraId = 0, .offsetCups = 0, .roi = {}},
            {.laneId = 1, .sensorId = 2, .cameraId = 1, .offsetCups = 0, .roi = {}}};
        TrackingModule module{std::make_unique<FrameSequenceTracker>(sensors),
                              TrackingOptions{.stateFile = file}};
        MessageBus bus;
        ModuleContext context{bus};
        ASSERT_TRUE(module.init(context).has_value());
        ASSERT_TRUE(module.start().has_value());
        module.stop(); // writes the state: nothing learned, so the stored phase is kept
    }
    std::ifstream in{file};
    const auto written = nlohmann::json::parse(in, nullptr, false);
    ASSERT_FALSE(written.is_discarded());
    EXPECT_EQ(written, saved);
    std::filesystem::remove_all(dir);
}

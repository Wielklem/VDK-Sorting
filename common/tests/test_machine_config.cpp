#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include <vsort/common/machine_config.hpp>

using nlohmann::json;

namespace {

json& sensors(json& config) {
    return config["lines"][0]["lanes"][0]["sensors"];
}

std::string errorOf(const json& config) {
    const auto result = vsort::MachineConfig::fromJson(config);
    EXPECT_FALSE(result.has_value());
    if (result.has_value()) {
        return {};
    }
    EXPECT_EQ(result.error().code, vsort::Errc::ValidationFailed);
    return result.error().message;
}

json twoLanes() {
    json config = vsort::machineDefaults();
    json lane2 = config["lines"][0]["lanes"][0];
    lane2["id"] = 2;
    lane2["name"] = "Lane 2";
    for (std::size_t i = 0; i < lane2["sensors"].size(); ++i) {
        lane2["sensors"][i]["id"] = 11 + i;
        lane2["sensors"][i]["roi_id"] = 2;
    }
    config["lines"][0]["lanes"].push_back(lane2);
    return config;
}

} // namespace

TEST(MachineConfig, DefaultsAreOneLaneWithFourCameras) {
    const auto machine = vsort::MachineConfig::fromJson(vsort::machineDefaults());
    ASSERT_TRUE(machine.has_value()) << machine.error().what();
    const auto lanes = machine->lanes();
    ASSERT_EQ(lanes.size(), 1U);
    ASSERT_EQ(lanes[0]->sensors.size(), 4U);
    for (std::uint16_t i = 0; i < 4; ++i) {
        const auto& s = lanes[0]->sensors[i];
        EXPECT_EQ(s.cameraId, i);
        EXPECT_EQ(s.offsetCups, 0U);
        EXPECT_EQ(s.roiId, 0U);
        EXPECT_TRUE(s.showInMonitor);
        EXPECT_TRUE(s.measurements.empty());
    }
}

TEST(MachineConfig, RoundTrip) {
    json config = vsort::machineDefaults();
    sensors(config)[1]["offset_cups"] = 7;
    sensors(config)[2]["offset_cups"] = 12;
    sensors(config)[3]["offset_cups"] = 12;
    sensors(config)[3]["show_in_monitor"] = false;
    sensors(config)[0]["measurements"] =
        json::parse(R"([{"key": "size_mm", "label": "Size", "unit": "mm"},
                        {"key": "dirt_pct", "label": "Dirt", "unit": "%"},
                        {"key": "count", "label": "Count", "unit": "", "format": "integer"},
                        {"key": "present", "label": "Cup", "unit": "", "format": "presence"}])");
    const auto machine = vsort::MachineConfig::fromJson(config);
    ASSERT_TRUE(machine.has_value()) << machine.error().what();
    EXPECT_EQ(machine->toJson(), config);
    const auto& m = machine->lines()[0].lanes[0].sensors[0].measurements;
    EXPECT_EQ(m[0].format, vsort::MeasurementFormat::Number); // not given: number
    EXPECT_EQ(m[2].format, vsort::MeasurementFormat::Integer);
    EXPECT_EQ(m[3].format, vsort::MeasurementFormat::Presence);
}

TEST(MachineConfig, RejectsSchemaViolation) {
    json config = vsort::machineDefaults();
    sensors(config)[0].erase("camera_id");
    EXPECT_NE(errorOf(config).find("camera_id"), std::string::npos);
}

TEST(MachineConfig, RejectsUnknownKind) {
    json config = vsort::machineDefaults();
    sensors(config)[0]["kind"] = "scale";
    errorOf(config);
}

TEST(MachineConfig, RejectsDuplicateIds) {
    json config = twoLanes();
    config["lines"][0]["lanes"][1]["id"] = 1;
    config["lines"][0]["lanes"][1]["sensors"][0]["id"] = 1;
    const auto message = errorOf(config);
    EXPECT_NE(message.find("duplicate lane ID 1"), std::string::npos) << message;
    EXPECT_NE(message.find("duplicate sensor ID 1"), std::string::npos) << message;
}

TEST(MachineConfig, RejectsCameraTwiceInOneLane) {
    json config = vsort::machineDefaults();
    sensors(config)[1]["camera_id"] = 0;
    EXPECT_NE(errorOf(config).find("used twice"), std::string::npos);
}

TEST(MachineConfig, RejectsDecreasingOffsets) {
    json config = vsort::machineDefaults();
    sensors(config)[1]["offset_cups"] = 5;
    sensors(config)[2]["offset_cups"] = 3;
    EXPECT_NE(errorOf(config).find("sensors[2].offset_cups"), std::string::npos);
}

TEST(MachineConfig, RejectsBadMeasurements) {
    json config = vsort::machineDefaults();
    sensors(config)[0]["measurements"] =
        json::parse(R"([{"key": "Size mm", "label": "Size", "unit": "mm"},
                        {"key": "dirt", "label": "Dirt", "unit": "%"},
                        {"key": "dirt", "label": "", "unit": "%"}])");
    const auto message = errorOf(config);
    EXPECT_NE(message.find("measurements[0].key"), std::string::npos) << message;
    EXPECT_NE(message.find("measurements[2].key: duplicate"), std::string::npos) << message;
    EXPECT_NE(message.find("measurements[2].label: empty"), std::string::npos) << message;

    sensors(config)[0]["measurements"] =
        json::parse(R"([{"key": "size_mm", "label": "Size", "unit": "mm", "format": "text"}])");
    EXPECT_NE(errorOf(config).find("format"), std::string::npos);
}

TEST(MachineConfig, RejectsEmptyLaneAndEmptyName) {
    json config = vsort::machineDefaults();
    config["lines"][0]["lanes"][0]["name"] = "";
    sensors(config) = json::array();
    const auto message = errorOf(config);
    EXPECT_NE(message.find("lanes[0].name: empty"), std::string::npos) << message;
    EXPECT_NE(message.find("at least one sensor"), std::string::npos) << message;
}

TEST(MachineConfig, AcceptsNoLines) {
    EXPECT_TRUE(vsort::MachineConfig::fromJson(json{{"lines", json::array()}}).has_value());
}

TEST(MachineConfig, CameraCanSeeSeveralLanes) {
    const auto machine = vsort::MachineConfig::fromJson(twoLanes());
    ASSERT_TRUE(machine.has_value()) << machine.error().what();
    const auto refs = machine->sensorsForCamera(0);
    ASSERT_EQ(refs.size(), 2U);
    EXPECT_EQ(refs[0].lane->id, 1);
    EXPECT_EQ(refs[1].lane->id, 2);
    EXPECT_EQ(refs[1].sensor->roiId, 2U);
    EXPECT_TRUE(machine->sensorsForCamera(9).empty());
    ASSERT_NE(machine->findLane(2), nullptr);
    EXPECT_EQ(machine->findLane(2)->name, "Lane 2");
    EXPECT_EQ(machine->findLane(3), nullptr);
}

TEST(MachineConfig, RoiReferences) {
    json config = vsort::machineDefaults();
    sensors(config)[0]["roi_id"] = 1;
    sensors(config)[1]["roi_id"] = 5;
    const auto machine = vsort::MachineConfig::fromJson(config);
    ASSERT_TRUE(machine.has_value());
    const json rois = json::parse(R"({"cameras": [{"camera_id": 0, "rois": [
      {"id": 1, "name": "ROI 1", "x": 0.1, "y": 0.2, "width": 0.3, "height": 0.4}]}]})");
    const auto result = vsort::checkRoiReferences(*machine, rois);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message.find("ROI 5 not found for camera 1"), std::string::npos);
    EXPECT_EQ(result.error().message.find("ROI 1"), std::string::npos);

    const auto defaults = vsort::MachineConfig::fromJson(vsort::machineDefaults());
    ASSERT_TRUE(defaults.has_value());
    EXPECT_TRUE(vsort::checkRoiReferences(*defaults, json{{"cameras", json::array()}}));
}

TEST(MachineConfig, RegistersAndLoads) {
    const auto dir = std::filesystem::temp_directory_path() / "vsort_machine_config_test";
    std::filesystem::remove_all(dir);
    {
        vsort::MessageBus bus;
        vsort::FileConfigStore store{dir, bus};
        ASSERT_TRUE(vsort::registerMachineConfig(store).has_value());
        const auto data = store.get(vsort::kMachineModule);
        ASSERT_TRUE(data.has_value());
        EXPECT_EQ(*data, vsort::machineDefaults());
        const auto machine = vsort::loadMachineConfig(store);
        ASSERT_TRUE(machine.has_value());
        EXPECT_EQ(machine->lanes().size(), 1U);
    }
    std::filesystem::remove_all(dir);
}

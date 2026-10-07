#include <filesystem>

#include <gtest/gtest.h>

#include <vsort/camera/camera_settings_config.hpp>

using nlohmann::json;
using namespace vsort;
using namespace vsort::camera;

namespace {

CameraSettings sample() {
    CameraSettings s;
    s.exposureUs = 7500.0;
    s.gainDb = 3.5;
    s.triggerMode = TriggerMode::FreeRun;
    s.triggerEdge = TriggerEdge::Falling;
    s.roi = Roi{.x = 8, .y = 16, .width = 640, .height = 480};
    return s;
}

class CameraSettingsStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() / "vsort_camera_settings_test";
        std::filesystem::remove_all(dir_);
    }
    void TearDown() override { std::filesystem::remove_all(dir_); }

    std::filesystem::path dir_;
    MessageBus bus_;
};

} // namespace

TEST(CameraSettingsConfig, DefaultsAreValidAndEmpty) {
    const auto parsed = settingsFromJson(cameraSettingsDefaults());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->empty());
}

TEST(CameraSettingsConfig, JsonRoundTrip) {
    const json config{{"cameras", json::array({cameraSettingsToJson(3, sample())})}};
    const auto parsed = settingsFromJson(config);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->size(), 1U);
    const CameraSettings& s = parsed->at(3);
    EXPECT_DOUBLE_EQ(s.exposureUs, 7500.0);
    EXPECT_DOUBLE_EQ(s.gainDb, 3.5);
    EXPECT_EQ(s.triggerMode, TriggerMode::FreeRun);
    EXPECT_EQ(s.triggerEdge, TriggerEdge::Falling);
    EXPECT_EQ(s.roi.x, 8U);
    EXPECT_EQ(s.roi.y, 16U);
    EXPECT_EQ(s.roi.width, 640U);
    EXPECT_EQ(s.roi.height, 480U);
}

TEST(CameraSettingsConfig, RejectsUnknownTrigger) {
    json entry = cameraSettingsToJson(0, sample());
    entry["trigger"] = "bogus";
    const auto parsed = settingsFromJson(json{{"cameras", json::array({entry})}});
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code, Errc::ValidationFailed);
}

TEST(CameraSettingsConfig, RejectsDuplicateId) {
    const json entry = cameraSettingsToJson(2, sample());
    const auto parsed = settingsFromJson(json{{"cameras", json::array({entry, entry})}});
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code, Errc::ValidationFailed);
}

TEST_F(CameraSettingsStoreTest, SaveKeepsTheOtherCameras) {
    FileConfigStore store{dir_, bus_};
    ASSERT_TRUE(registerCameraSettings(store).has_value());
    ASSERT_TRUE(saveCameraSettings(store, 1, sample(), "test").has_value());
    CameraSettings other = sample();
    other.exposureUs = 1234.0;
    ASSERT_TRUE(saveCameraSettings(store, 0, other, "test").has_value());
    other.exposureUs = 4321.0;
    ASSERT_TRUE(saveCameraSettings(store, 0, other, "test").has_value()); // replaces ID 0

    const auto loaded = loadCameraSettings(store);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->size(), 2U);
    EXPECT_DOUBLE_EQ(loaded->at(0).exposureUs, 4321.0);
    EXPECT_DOUBLE_EQ(loaded->at(1).exposureUs, 7500.0);
}

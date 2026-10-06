#include <filesystem>

#include <gtest/gtest.h>

#include <vsort/common/roi_config.hpp>

using nlohmann::json;

namespace {

const json kValid = json::parse(R"({"cameras": [{"camera_id": 3, "rois": [
  {"id": 1, "name": "ROI 1", "x": 0.1, "y": 0.2, "width": 0.3, "height": 0.4}]}]})");

} // namespace

TEST(RoiConfig, DefaultsAreValid) {
    EXPECT_TRUE(vsort::validateJson(vsort::roiSchema(), vsort::roiDefaults()).has_value());
}

TEST(RoiConfig, AcceptsRois) {
    EXPECT_TRUE(vsort::validateJson(vsort::roiSchema(), kValid).has_value());
}

TEST(RoiConfig, RejectsRoiOutsideTheImage) {
    json bad = kValid;
    bad["cameras"][0]["rois"][0]["x"] = 1.5;
    const auto result = vsort::validateJson(vsort::roiSchema(), bad);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, vsort::Errc::ValidationFailed);
}

TEST(RoiConfig, RejectsMissingSize) {
    json bad = kValid;
    bad["cameras"][0]["rois"][0].erase("width");
    EXPECT_FALSE(vsort::validateJson(vsort::roiSchema(), bad).has_value());
}

TEST(RoiConfig, RejectsUnknownKey) {
    json bad = kValid;
    bad["cameras"][0]["rois"][0]["extra"] = 1;
    EXPECT_FALSE(vsort::validateJson(vsort::roiSchema(), bad).has_value());
}

TEST(RoiConfig, RegistersWithDefaults) {
    const auto dir = std::filesystem::temp_directory_path() / "vsort_roi_config_test";
    std::filesystem::remove_all(dir);
    {
        vsort::MessageBus bus;
        vsort::FileConfigStore store{dir, bus};
        ASSERT_TRUE(vsort::registerRoiConfig(store).has_value());
        const auto data = store.get(vsort::kRoiModule);
        ASSERT_TRUE(data.has_value());
        EXPECT_EQ(*data, vsort::roiDefaults());
    }
    std::filesystem::remove_all(dir);
}

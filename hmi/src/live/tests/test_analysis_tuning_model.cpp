#include <QList>
#include <QString>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "fake_service.hpp"
#include "live/analysis_tuning_model.hpp"
#include "live/service_client.hpp"
#include "test_util.hpp"

namespace {

using namespace vsort::hmi;
using namespace vsort::hmi::test;
using nlohmann::json;

// Camera 0: sensors 1 and 3 (two lanes). Camera 1: sensor 2. Camera 2: none.
const std::string kMachine = R"({"lines": [{"id": 1, "name": "Line 1", "lanes": [
  {"id": 1, "name": "Lane 1", "sensors": [
    {"id": 1, "name": "Cam A", "kind": "camera", "camera_id": 0, "roi_id": 0,
     "offset_cups": 0, "show_in_monitor": true, "measurements": []},
    {"id": 2, "name": "Cam B", "kind": "camera", "camera_id": 1, "roi_id": 0,
     "offset_cups": 5, "show_in_monitor": true, "measurements": []}]},
  {"id": 2, "name": "Lane 2", "sensors": [
    {"id": 3, "name": "Cam A lane 2", "kind": "camera", "camera_id": 0, "roi_id": 0,
     "offset_cups": 0, "show_in_monitor": true, "measurements": []}]}]}]})";

// The service defaults, with an override for sensor 2.
const std::string kAnalysis = R"({"enabled": true, "debug_every_n": 0, "debug_max_images": 200,
  "defaults": {"hsv_lower": [60, 70, 0], "hsv_upper": [105, 170, 255], "morph_kernel_px": 3,
    "erode_iterations": 1, "dilate_iterations": 1,
    "roi_buffer_px": {"left": 50, "top": 50, "right": 50, "bottom": 50},
    "min_blob_area_px": 100, "min_diameter_px": 100.0, "min_hull_area_px": 6000.0,
    "min_solidity": 0.85, "max_aspect_ratio": 3.0, "mm_per_px": 1.0, "roi_width_mm": 200.0},
  "sensors": [{"sensor_id": 2, "params": {"hsv_lower": [10, 20, 30], "hsv_upper": [40, 50, 60],
    "morph_kernel_px": 5, "erode_iterations": 1, "dilate_iterations": 1,
    "roi_buffer_px": {"left": 0, "top": 0, "right": 0, "bottom": 0},
    "min_blob_area_px": 100, "min_diameter_px": 100.0, "min_hull_area_px": 6000.0,
    "min_solidity": 0.85, "max_aspect_ratio": 3.0, "mm_per_px": 1.0, "roi_width_mm": 0.0}}]})";

class AnalysisTuningModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        service.setConfigJson("machine", kMachine);
        service.setConfigJson("analysis", kAnalysis);
        model.selectCamera(0);
        client.start();
        ASSERT_TRUE(spinUntil([&] { return model.loaded() && !model.sensors().isEmpty(); },
                              [this] { service.pump(); }));
    }

    [[nodiscard]] json saved() const { return json::parse(service.configJson("analysis")); }

    static const json* sensorEntry(const json& config, int id) {
        for (const auto& e : config.at("sensors")) {
            if (e.at("sensor_id") == id) {
                return &e;
            }
        }
        return nullptr;
    }

    FakeService service{{}};
    ServiceClient client{{.host = QStringLiteral("127.0.0.1"),
                          .commandPort = FakeService::kCommandPort,
                          .eventPort = FakeService::kEventPort}};
    AnalysisTuningModel model{client};
};

} // namespace

TEST_F(AnalysisTuningModelTest, ShowsTheRangeOfTheCameraSensors) {
    EXPECT_EQ(model.sensors(), QStringLiteral("Cam A, Cam A lane 2"));
    EXPECT_EQ(model.hsvLower(), (QList<int>{60, 70, 0})); // sensor 1 uses the defaults
    EXPECT_EQ(model.hsvUpper(), (QList<int>{105, 170, 255}));
    EXPECT_TRUE(model.canSave());

    model.selectCamera(1); // sensor 2 has an override
    EXPECT_EQ(model.hsvLower(), (QList<int>{10, 20, 30}));
    EXPECT_EQ(model.hsvUpper(), (QList<int>{40, 50, 60}));

    model.selectCamera(2);
    EXPECT_TRUE(model.sensors().isEmpty());
    EXPECT_FALSE(model.canSave());
}

TEST_F(AnalysisTuningModelTest, EditsAreClampedAndKeepLowerBelowUpper) {
    model.setValue(0, 0, 300); // H lower above 179 and above the upper bound
    EXPECT_EQ(model.hsvLower()[0], 179);
    EXPECT_EQ(model.hsvUpper()[0], 179);
    model.setValue(1, 2, -5); // V upper below 0 drags the lower bound along
    EXPECT_EQ(model.hsvUpper()[2], 0);
    EXPECT_EQ(model.hsvLower()[2], 0);
    EXPECT_TRUE(model.dirty());

    model.selectCamera(1); // another camera: edits discarded
    EXPECT_FALSE(model.dirty());
}

TEST_F(AnalysisTuningModelTest, SaveWritesOverridesForEverySensorOfTheCamera) {
    model.setValue(0, 0, 70);
    model.setValue(1, 1, 200);
    model.save();
    ASSERT_TRUE(spinUntil([&] { return !model.dirty() && model.status().startsWith("Saved"); },
                          [this] { service.pump(); }));

    const auto config = saved();
    for (const int id : {1, 3}) {
        const auto* entry = sensorEntry(config, id);
        ASSERT_NE(entry, nullptr) << id;
        EXPECT_EQ(entry->at("params").at("hsv_lower"), json::array({70, 70, 0}));
        EXPECT_EQ(entry->at("params").at("hsv_upper"), json::array({105, 200, 255}));
        EXPECT_EQ(entry->at("params").at("roi_width_mm"), 200.0); // copied from the defaults
    }
    const auto* other = sensorEntry(config, 2); // camera 1: untouched
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(other->at("params").at("hsv_lower"), json::array({10, 20, 30}));
    EXPECT_EQ(config.at("defaults").at("hsv_lower"), json::array({60, 70, 0}));
}

TEST_F(AnalysisTuningModelTest, ReloadDiscardsEdits) {
    model.setValue(0, 1, 10);
    model.reload();
    ASSERT_TRUE(spinUntil([&] { return !model.dirty(); }, [this] { service.pump(); }));
    EXPECT_EQ(model.hsvLower(), (QList<int>{60, 70, 0}));
}

TEST_F(AnalysisTuningModelTest, DetectorParamsAreEditedAndSavedPerCamera) {
    auto p = model.params();
    EXPECT_DOUBLE_EQ(p.value(QStringLiteral("min_solidity")).toDouble(), 0.85);
    EXPECT_DOUBLE_EQ(p.value(QStringLiteral("roi_buffer_px.left")).toDouble(), 50.0);
    EXPECT_FALSE(p.contains(QStringLiteral("hsv_lower"))); // HSV goes through hsvLower/hsvUpper

    model.setParam(QStringLiteral("min_solidity"), 0.7);
    model.setParam(QStringLiteral("erode_iterations"), 2.4); // integer: rounded
    model.setParam(QStringLiteral("roi_buffer_px.left"), 20);
    model.setParam(QStringLiteral("no_such_key"), 1); // ignored
    EXPECT_TRUE(model.dirty());
    p = model.params();
    EXPECT_DOUBLE_EQ(p.value(QStringLiteral("min_solidity")).toDouble(), 0.7);
    EXPECT_DOUBLE_EQ(p.value(QStringLiteral("erode_iterations")).toDouble(), 2.0);

    model.save();
    ASSERT_TRUE(spinUntil([&] { return !model.dirty() && model.status().startsWith("Saved"); },
                          [this] { service.pump(); }));
    const auto config = saved();
    for (const int id : {1, 3}) {
        const auto& params = sensorEntry(config, id)->at("params");
        EXPECT_DOUBLE_EQ(params.at("min_solidity").get<double>(), 0.7) << id;
        EXPECT_TRUE(params.at("erode_iterations").is_number_integer()) << id;
        EXPECT_EQ(params.at("erode_iterations").get<int>(), 2) << id;
        EXPECT_EQ(params.at("roi_buffer_px").at("left").get<int>(), 20) << id;
        EXPECT_EQ(params.at("roi_buffer_px").at("top").get<int>(), 50) << id; // untouched
    }
    EXPECT_DOUBLE_EQ(config.at("defaults").at("min_solidity").get<double>(), 0.85);
    EXPECT_DOUBLE_EQ(model.params().value(QStringLiteral("min_solidity")).toDouble(), 0.7);
}

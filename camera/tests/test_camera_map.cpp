#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <vsort/camera/camera_map.hpp>

using nlohmann::json;
using namespace vsort;
using namespace vsort::camera;

namespace {

CameraMap makeMap() {
    auto map = CameraMap::create({{.serial = "SN-B", .id = 1}, {.serial = "SN-A", .id = 0}});
    return *map;
}

DiscoveredCamera found(const char* serial) {
    DiscoveredCamera c;
    c.serial = serial;
    return c;
}

class CameraMapStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() / "vsort_camera_map_test";
        std::filesystem::remove_all(dir_);
    }
    void TearDown() override { std::filesystem::remove_all(dir_); }

    std::filesystem::path dir_;
    MessageBus bus_;
};

} // namespace

TEST(CameraMap, LooksUpBothDirections) {
    const auto map = makeMap();
    EXPECT_EQ(map.idFor("SN-A"), 0);
    EXPECT_EQ(map.idFor("SN-B"), 1);
    EXPECT_FALSE(map.idFor("SN-X").has_value());
    EXPECT_EQ(map.serialFor(1), "SN-B");
    EXPECT_FALSE(map.serialFor(7).has_value());
}

TEST(CameraMap, RejectsDuplicatesAndEmptySerial) {
    EXPECT_EQ(CameraMap::create({{"A", 0}, {"A", 1}}).error().code, Errc::ValidationFailed);
    EXPECT_EQ(CameraMap::create({{"A", 0}, {"B", 0}}).error().code, Errc::ValidationFailed);
    EXPECT_EQ(CameraMap::create({{"", 0}}).error().code, Errc::ValidationFailed);
    EXPECT_TRUE(CameraMap::create({}).has_value());
}

TEST(CameraMap, JsonRoundTrip) {
    const auto map = makeMap();
    const auto back = CameraMap::fromJson(map.toJson());
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->idFor("SN-B"), 1);
    EXPECT_EQ(back->toJson(), map.toJson());
}

TEST(CameraMap, FromJsonRejectsSchemaViolations) {
    for (const char* bad : {R"({})", R"({"cameras": [{"serial": "A"}]})",
                            R"({"cameras": [{"serial": "A", "id": 70000}]})",
                            R"({"cameras": [{"serial": "A", "id": -1}]})",
                            R"({"cameras": [{"serial": "A", "id": 0, "x": 1}]})"}) {
        EXPECT_EQ(CameraMap::fromJson(json::parse(bad)).error().code, Errc::ValidationFailed)
            << bad;
    }
}

TEST(AssignCameras, SplitsMappedUnmappedAndMissing) {
    const auto map = makeMap();
    const auto result = assignCameras(map, {found("SN-B"), found("SN-X")});
    ASSERT_EQ(result.mapped.size(), 1U);
    EXPECT_EQ(result.mapped[0].id, 1);
    EXPECT_EQ(result.mapped[0].camera.serial, "SN-B");
    ASSERT_EQ(result.unmappedSerials.size(), 1U);
    EXPECT_EQ(result.unmappedSerials[0], "SN-X");
    ASSERT_EQ(result.missing.size(), 1U);
    EXPECT_EQ(result.missing[0].serial, "SN-A");
}

TEST(AssignCameras, MappedAreSortedById) {
    const auto result = assignCameras(makeMap(), {found("SN-B"), found("SN-A")});
    ASSERT_EQ(result.mapped.size(), 2U);
    EXPECT_EQ(result.mapped[0].id, 0);
    EXPECT_EQ(result.mapped[1].id, 1);
    EXPECT_TRUE(result.missing.empty());
}

TEST_F(CameraMapStoreTest, DefaultsToEmptyMap) {
    FileConfigStore store{dir_, bus_};
    ASSERT_TRUE(registerCameraMap(store).has_value());
    const auto map = loadCameraMap(store);
    ASSERT_TRUE(map.has_value());
    EXPECT_TRUE(map->entries().empty());
}

TEST_F(CameraMapStoreTest, SavedMappingIsLoadedAfterRestart) {
    {
        FileConfigStore store{dir_, bus_};
        ASSERT_TRUE(registerCameraMap(store).has_value());
        ASSERT_TRUE(store.set(kCameraMapModule, makeMap().toJson(), "test").has_value());
    }
    FileConfigStore store{dir_, bus_};
    ASSERT_TRUE(registerCameraMap(store).has_value());
    const auto map = loadCameraMap(store);
    ASSERT_TRUE(map.has_value());
    EXPECT_EQ(map->idFor("SN-A"), 0);
    EXPECT_EQ(map->idFor("SN-B"), 1);
}

TEST_F(CameraMapStoreTest, DuplicateIdInStoredConfigFailsToLoad) {
    FileConfigStore store{dir_, bus_};
    ASSERT_TRUE(registerCameraMap(store).has_value());
    // Passes the schema, but the IDs collide: caught by loadCameraMap.
    ASSERT_TRUE(
        store
            .set(kCameraMapModule,
                 json::parse(R"({"cameras":[{"serial":"A","id":0},{"serial":"B","id":0}]})"),
                 "test")
            .has_value());
    EXPECT_EQ(loadCameraMap(store).error().code, Errc::ValidationFailed);
}

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>

#include <gtest/gtest.h>

#include <vsort/camera/camera_map.hpp>
#include <vsort/camera/camera_settings_config.hpp>

#include "camera/camera_manager.hpp"
#include "test_camera_fakes.hpp"

using namespace vsort;
using namespace vsort::service;
using namespace std::chrono_literals;
using vsort::testutil::FakeBackend;
using vsort::testutil::FakeWorld;
using vsort::testutil::waitFor;

namespace {

class CameraManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() / "vsort_camera_manager_test";
        std::filesystem::remove_all(dir_);
        store_ = std::make_unique<FileConfigStore>(dir_, bus_);
        ASSERT_TRUE(camera::registerCameraMap(*store_).has_value());
        ASSERT_TRUE(camera::registerCameraSettings(*store_).has_value());
    }
    void TearDown() override {
        manager_.reset();
        store_.reset();
        std::filesystem::remove_all(dir_);
    }

    void setMap(std::vector<camera::CameraMapEntry> entries) {
        const auto map = camera::CameraMap::create(std::move(entries));
        ASSERT_TRUE(map.has_value());
        ASSERT_TRUE(store_->set(camera::kCameraMapModule, map->toJson(), "test").has_value());
    }

    CameraManager& make(CameraManagerOptions options = {}, bool persistent = true,
                        std::vector<camera::CameraMapEntry> fixed = {}) {
        options.scanInterval = 20ms;
        manager_ = std::make_unique<CameraManager>(
            std::make_unique<FakeBackend>(world_, persistent, std::move(fixed)),
            persistent ? store_.get() : nullptr, options);
        manager_->addFrameSink([this](const camera::Frame& frame) {
            const std::scoped_lock lock{framesMutex_};
            ++frames_[frame.meta.cameraIndex];
        });
        manager_->setOnListChanged([this] { ++listChanges_; });
        return *manager_;
    }

    [[nodiscard]] std::uint64_t framesOf(std::uint16_t id) {
        const std::scoped_lock lock{framesMutex_};
        return frames_[id];
    }

    [[nodiscard]] bool streaming(std::size_t count) const {
        const auto cameras = manager_->list();
        if (cameras.size() != count) {
            return false;
        }
        return std::ranges::all_of(cameras, [](const CameraListEntry& cam) {
            return cam.state == CameraRuntimeState::Streaming;
        });
    }

    std::filesystem::path dir_;
    testutil::FakeWorld world_;
    MessageBus bus_;
    std::unique_ptr<FileConfigStore> store_;
    std::unique_ptr<CameraManager> manager_;
    std::mutex framesMutex_;
    std::map<std::uint16_t, std::uint64_t> frames_;
    std::atomic<int> listChanges_{0};
};

} // namespace

TEST_F(CameraManagerTest, EmptyMapIsFilledBySerialNumberAndCamerasStream) {
    world_.plug("SN-B");
    world_.plug("SN-A");
    auto& manager = make();
    ASSERT_TRUE(manager.start().has_value());
    ASSERT_TRUE(waitFor([&] { return streaming(2); }));

    const auto cameras = manager.list();
    EXPECT_EQ(cameras[0].id, 0U);
    EXPECT_EQ(cameras[0].serial, "SN-A");
    EXPECT_EQ(cameras[0].model, "FAKE");
    EXPECT_EQ(cameras[1].id, 1U);
    EXPECT_EQ(cameras[1].serial, "SN-B");

    const auto saved = camera::loadCameraMap(*store_);
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->idFor("SN-A"), 0U);
    EXPECT_EQ(saved->idFor("SN-B"), 1U);

    // Frames carry the logical camera ID.
    EXPECT_TRUE(waitFor([&] { return framesOf(0) > 0 && framesOf(1) > 0; }));
    EXPECT_GT(listChanges_.load(), 0);
}

TEST_F(CameraManagerTest, MappedCameraThatAppearsLaterIsConnectedAndAnnounced) {
    setMap({{.serial = "SN-A", .id = 0}, {.serial = "SN-B", .id = 1}});
    world_.plug("SN-A");
    auto& manager = make();
    ASSERT_TRUE(manager.start().has_value());
    ASSERT_TRUE(waitFor([&] {
        const auto cameras = manager.list();
        return cameras.size() == 2 && cameras[0].state == CameraRuntimeState::Streaming;
    }));
    EXPECT_EQ(manager.list()[1].state, CameraRuntimeState::Closed);
    EXPECT_EQ(manager.health().state, HealthState::Degraded);

    const int before = listChanges_.load();
    world_.plug("SN-B");
    ASSERT_TRUE(waitFor([&] { return streaming(2); }));
    EXPECT_GT(listChanges_.load(), before);
    EXPECT_EQ(manager.health().state, HealthState::Ok);
}

TEST_F(CameraManagerTest, UnmappedCameraIsIgnoredWhenTheMapHasEntries) {
    setMap({{.serial = "SN-A", .id = 0}});
    world_.plug("SN-A");
    world_.plug("SN-X");
    auto& manager = make();
    ASSERT_TRUE(manager.start().has_value());
    ASSERT_TRUE(waitFor([&] { return streaming(1); }));
    EXPECT_EQ(manager.list().size(), 1U);
}

TEST_F(CameraManagerTest, AppliedSettingsAreSavedAndReappliedAfterARestart) {
    setMap({{.serial = "SN-A", .id = 0}});
    world_.plug("SN-A");
    {
        auto& manager = make();
        ASSERT_TRUE(manager.start().has_value());
        ASSERT_TRUE(waitFor([&] { return streaming(1); }));

        camera::CameraSettings wanted;
        wanted.exposureUs = 7000.0;
        wanted.gainDb = 3.0;
        ASSERT_TRUE(manager.apply(0, wanted).has_value());
        EXPECT_DOUBLE_EQ(world_.applied("SN-A")->exposureUs, 7000.0);
        const auto read = manager.settings(0);
        ASSERT_TRUE(read.has_value());
        EXPECT_DOUBLE_EQ(read->exposureUs, 7000.0);

        const auto saved = camera::loadCameraSettings(*store_);
        ASSERT_TRUE(saved.has_value());
        EXPECT_DOUBLE_EQ(saved->at(0).exposureUs, 7000.0);
        manager.stop();
    }

    world_.clearApplied();
    auto& again = make();
    ASSERT_TRUE(again.start().has_value());
    ASSERT_TRUE(waitFor([&] { return streaming(1); }));
    ASSERT_TRUE(world_.applied("SN-A").has_value());
    EXPECT_DOUBLE_EQ(world_.applied("SN-A")->exposureUs, 7000.0);
    EXPECT_DOUBLE_EQ(world_.applied("SN-A")->gainDb, 3.0);
}

TEST_F(CameraManagerTest, SettingsTheCameraRejectsAreNotSaved) {
    setMap({{.serial = "SN-A", .id = 0}});
    world_.plug("SN-A");
    auto& manager = make();
    ASSERT_TRUE(manager.start().has_value());
    ASSERT_TRUE(waitFor([&] { return streaming(1); }));

    camera::CameraSettings bad;
    bad.exposureUs = -1.0;
    const auto result = manager.apply(0, bad);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, Errc::InvalidArgument);
    const auto saved = camera::loadCameraSettings(*store_);
    ASSERT_TRUE(saved.has_value());
    EXPECT_TRUE(saved->empty());
}

TEST_F(CameraManagerTest, ForcedFreeRunIsNotSaved) {
    setMap({{.serial = "SN-A", .id = 0}});
    world_.plug("SN-A");
    CameraManagerOptions options;
    options.forceFreeRun = true;
    auto& manager = make(options);
    ASSERT_TRUE(manager.start().has_value());
    ASSERT_TRUE(waitFor([&] { return streaming(1); }));
    EXPECT_EQ(world_.applied("SN-A")->triggerMode, camera::TriggerMode::FreeRun);

    camera::CameraSettings wanted = *manager.settings(0); // the HMI sends back what it read
    wanted.exposureUs = 6000.0;
    ASSERT_TRUE(manager.apply(0, wanted).has_value());
    EXPECT_EQ(world_.applied("SN-A")->triggerMode, camera::TriggerMode::FreeRun);
    const auto saved = camera::loadCameraSettings(*store_);
    ASSERT_TRUE(saved.has_value());
    EXPECT_DOUBLE_EQ(saved->at(0).exposureUs, 6000.0);
    EXPECT_EQ(saved->at(0).triggerMode, camera::TriggerMode::Hardware);
}

TEST_F(CameraManagerTest, NonPersistentBackendUsesItsFixedMapAndNoConfig) {
    world_.plug("SN-R");
    auto& manager = make({}, false, {{.serial = "SN-R", .id = 5}});
    ASSERT_TRUE(manager.start().has_value());
    ASSERT_TRUE(waitFor([&] { return streaming(1); }));
    EXPECT_EQ(manager.list()[0].id, 5U);
    camera::CameraSettings wanted;
    wanted.exposureUs = 2000.0;
    EXPECT_TRUE(manager.apply(5, wanted).has_value());
    const auto saved = camera::loadCameraSettings(*store_);
    ASSERT_TRUE(saved.has_value());
    EXPECT_TRUE(saved->empty());
}

TEST_F(CameraManagerTest, UnknownAndDisconnectedCamerasGiveErrors) {
    setMap({{.serial = "SN-A", .id = 0}});
    auto& manager = make();
    ASSERT_TRUE(manager.start().has_value());
    EXPECT_EQ(manager.settings(99).error().code, Errc::NotFound);
    EXPECT_EQ(manager.settings(0).error().code, Errc::DeviceError); // SN-A is not plugged in
    EXPECT_EQ(manager.apply(0, camera::CameraSettings{}).error().code, Errc::DeviceError);
}

TEST_F(CameraManagerTest, StopClosesTheCamerasAndIsIdempotent) {
    setMap({{.serial = "SN-A", .id = 0}});
    world_.plug("SN-A");
    auto& manager = make();
    ASSERT_TRUE(manager.start().has_value());
    ASSERT_TRUE(waitFor([&] { return streaming(1); }));
    manager.stop();
    manager.stop();
    EXPECT_EQ(manager.list()[0].state, CameraRuntimeState::Closed);
    EXPECT_EQ(manager.apply(0, camera::CameraSettings{}).error().code, Errc::DeviceError);

    const auto frames = framesOf(0);
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(framesOf(0), frames); // no frame after stop()
}

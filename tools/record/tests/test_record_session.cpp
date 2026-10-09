#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <sstream>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <vsort/camera/camera_settings_config.hpp>
#include <vsort/common/config_store.hpp>
#include <vsort/common/message_bus.hpp>
#include <vsort/common/timestamp.hpp>

#include "record_session.hpp"

namespace {

using namespace vsort;
using namespace vsort::record;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

constexpr std::uint32_t kWidth = 8;
constexpr std::uint32_t kHeight = 4;
constexpr std::string_view kMissingSerial = "MISSING";

// Delivers a frame every 500 us from its own thread once started.
class FakeCamera final : public camera::ICamera {
public:
    FakeCamera() = default;
    FakeCamera(const FakeCamera&) = delete;
    FakeCamera& operator=(const FakeCamera&) = delete;
    FakeCamera(FakeCamera&&) = delete;
    FakeCamera& operator=(FakeCamera&&) = delete;
    ~FakeCamera() override { stop(); }

    [[nodiscard]] Result<> open(std::string_view serial) override {
        if (serial == kMissingSerial) {
            return makeError(Errc::NotFound, "no such camera");
        }
        serial_ = std::string{serial};
        open_ = true;
        return {};
    }

    void close() noexcept override {
        stop();
        open_ = false;
    }

    [[nodiscard]] bool isOpen() const noexcept override { return open_; }

    [[nodiscard]] Result<camera::CameraInfo> info() const override {
        return camera::CameraInfo{
            .serial = serial_, .model = "FAKE", .sensorWidth = kWidth, .sensorHeight = kHeight};
    }

    [[nodiscard]] Result<camera::CameraSettings> settings() const override { return settings_; }

    [[nodiscard]] Result<> applySettings(const camera::CameraSettings& settings) override {
        settings_ = settings;
        return {};
    }

    void setFrameCallback(camera::FrameCallback callback) override {
        callback_ = std::move(callback);
    }

    [[nodiscard]] Result<> start() override {
        if (!open_) {
            return makeError(Errc::InvalidArgument, "not open");
        }
        streaming_ = true;
        worker_ = std::jthread{[this](const std::stop_token& token) { run(token); }};
        return {};
    }

    void stop() noexcept override {
        worker_.request_stop();
        if (worker_.joinable()) {
            worker_.join();
        }
        streaming_ = false;
    }

    [[nodiscard]] bool isStreaming() const noexcept override { return streaming_; }

private:
    void run(const std::stop_token& token) {
        std::uint64_t id = 0;
        while (!token.stop_requested()) {
            ++id;
            auto pixels = std::make_shared<std::vector<std::byte>>(std::size_t{kWidth} * kHeight,
                                                                   std::byte{0x5A});
            camera::Frame frame;
            frame.meta = camera::FrameMetadata{.frameId = FrameId{id},
                                               .hostTimestamp = Timestamp::now(),
                                               .deviceTimestampNs = id * 1000,
                                               .width = kWidth,
                                               .height = kHeight,
                                               .strideBytes = kWidth,
                                               .pixelFormat = camera::PixelFormat::Mono8};
            frame.data = std::span<const std::byte>{*pixels};
            frame.owner = pixels;
            callback_(frame);
            std::this_thread::sleep_for(500us);
        }
    }

    std::string serial_;
    camera::CameraSettings settings_;
    camera::FrameCallback callback_;
    std::atomic<bool> open_{false};
    std::atomic<bool> streaming_{false};
    std::jthread worker_; // last: destroyed first
};

camera::CameraFactory fakeFactory() {
    return []() -> std::unique_ptr<camera::ICamera> { return std::make_unique<FakeCamera>(); };
}

std::string readFile(const fs::path& path) {
    const std::ifstream in{path, std::ios::binary};
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

class RecordSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("vsort_record_test_" + std::to_string(WallTime::now().ns()));
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    [[nodiscard]] RecordPlan makeTestPlan() const {
        RecordPlan plan;
        plan.outDir = root_;
        plan.label = "unit test";
        plan.cameras = {CameraSpec{.index = 0, .serial = "SN0"},
                        CameraSpec{.index = 1, .serial = "SN1"}};
        plan.cameras[0].settings.exposureUs = 5000.0;
        plan.cameras[1].settings.exposureUs = 750.0;
        plan.cameras[1].settings.gainDb = 10.0;
        plan.queueDepth = 64;
        return plan;
    }

    fs::path root_;
};

} // namespace

TEST(RecordPool, PoolIsLargerThanTheRoundedUpQueue) {
    EXPECT_EQ(framePoolSize(1), 9U);
    EXPECT_EQ(framePoolSize(32), 40U);
    EXPECT_EQ(framePoolSize(33), 72U);
    EXPECT_EQ(framePoolSize(0), 9U);
}

TEST(RecordPool, MakePlanCopiesTheOptions) {
    Options options;
    options.cameras = {CameraSpec{.index = 3, .serial = "SN3"}};
    options.outDir = "out";
    options.label = "x";
    options.exposureUs = 1234.0;
    options.gainDb = 2.0;
    options.trigger = camera::TriggerMode::FreeRun;
    options.duration = 5s;
    options.framesPerCamera = 7;
    options.queueDepth = 16;

    const auto plan = makePlan(options);
    ASSERT_TRUE(plan.has_value()) << plan.error().what();
    EXPECT_EQ(plan->outDir, fs::path{"out"});
    ASSERT_EQ(plan->cameras.size(), 1U);
    const auto& settings = plan->cameras[0].settings;
    EXPECT_DOUBLE_EQ(settings.exposureUs, 1234.0);
    EXPECT_DOUBLE_EQ(settings.gainDb, 2.0);
    EXPECT_EQ(settings.triggerMode, camera::TriggerMode::FreeRun);
    EXPECT_EQ(plan->duration, 5000ms);
    EXPECT_EQ(plan->framesPerCamera, 7U);
    EXPECT_EQ(plan->queueDepth, 16U);
}

TEST(RecordPool, MakePlanWithoutOverridesUsesTheDefaults) {
    Options options;
    options.cameras = {CameraSpec{.index = 0, .serial = "SN0"}};
    const auto plan = makePlan(options);
    ASSERT_TRUE(plan.has_value()) << plan.error().what();
    const auto& settings = plan->cameras[0].settings;
    EXPECT_DOUBLE_EQ(settings.exposureUs, 10000.0);
    EXPECT_DOUBLE_EQ(settings.gainDb, 0.0);
    EXPECT_EQ(settings.triggerMode, camera::TriggerMode::Hardware);
}

TEST(RecordPool, MakePlanUsesTheSavedSettingsPerCamera) {
    SavedSettings saved;
    saved[0] =
        camera::CameraSettings{.exposureUs = 750.0,
                               .gainDb = 10.0,
                               .roi = camera::Roi{.x = 8, .y = 4, .width = 640, .height = 480},
                               .triggerMode = camera::TriggerMode::Hardware,
                               .triggerEdge = camera::TriggerEdge::Falling};
    saved[1].exposureUs = 500.0;
    saved[1].gainDb = 14.0;

    Options options;
    options.cameras = {CameraSpec{.index = 0, .serial = "SN0"},
                       CameraSpec{.index = 1, .serial = "SN1"}};
    auto plan = makePlan(options, &saved);
    ASSERT_TRUE(plan.has_value()) << plan.error().what();
    const auto& cam0 = plan->cameras[0].settings;
    EXPECT_DOUBLE_EQ(cam0.exposureUs, 750.0);
    EXPECT_DOUBLE_EQ(cam0.gainDb, 10.0);
    EXPECT_EQ(cam0.triggerEdge, camera::TriggerEdge::Falling);
    EXPECT_EQ(cam0.roi.width, 640U);
    EXPECT_DOUBLE_EQ(plan->cameras[1].settings.gainDb, 14.0);

    options.gainDb = 3.0; // an override replaces the saved value for every camera
    plan = makePlan(options, &saved);
    ASSERT_TRUE(plan.has_value()) << plan.error().what();
    EXPECT_DOUBLE_EQ(plan->cameras[0].settings.gainDb, 3.0);
    EXPECT_DOUBLE_EQ(plan->cameras[1].settings.gainDb, 3.0);
    EXPECT_DOUBLE_EQ(plan->cameras[1].settings.exposureUs, 500.0);
    EXPECT_EQ(describeSettings(plan->cameras[0].settings),
              "exposure 750 us, gain 3 dB, trigger hardware (falling), roi 640x480 at 8,4");
}

TEST(RecordPool, MakePlanRejectsMissingSettingsAndSoftwareTrigger) {
    SavedSettings saved;
    saved[0].triggerMode = camera::TriggerMode::Software;
    Options options;
    options.cameras = {CameraSpec{.index = 1, .serial = "SN1"}};
    const auto missing = makePlan(options, &saved);
    ASSERT_FALSE(missing.has_value());
    EXPECT_NE(missing.error().message.find("camera 1: no saved settings"), std::string::npos);

    options.cameras = {CameraSpec{.index = 0, .serial = "SN0"}};
    const auto software = makePlan(options, &saved);
    ASSERT_FALSE(software.has_value());
    EXPECT_NE(software.error().message.find("software trigger"), std::string::npos);

    options.trigger = camera::TriggerMode::FreeRun; // the override makes it recordable
    EXPECT_TRUE(makePlan(options, &saved).has_value());
}

TEST(RecordReportTest, CleanNeedsFramesAndNoLoss) {
    RecordReport report;
    EXPECT_FALSE(report.clean()); // no cameras

    CameraResult cam;
    cam.health.framesReceived = 10;
    report.cameras.push_back(cam);
    EXPECT_TRUE(report.clean());

    report.cameras[0].health.framesReceived = 0;
    EXPECT_FALSE(report.clean());
    report.cameras[0].health.framesReceived = 10;

    report.cameras[0].health.framesMissed = 1;
    EXPECT_FALSE(report.clean());
    report.cameras[0].health.framesMissed = 0;

    report.cameras[0].health.framesDropped = 1;
    EXPECT_FALSE(report.clean());
    report.cameras[0].health.framesDropped = 0;

    report.stats.dropped = 1;
    EXPECT_FALSE(report.clean());
    report.stats.dropped = 0;

    report.stats.failed = 1;
    EXPECT_FALSE(report.clean());
}

TEST_F(RecordSessionTest, RejectsInvalidPlans) {
    std::ostringstream out;

    RecordPlan noCameras = makeTestPlan();
    noCameras.cameras.clear();
    const auto a = runRecording(noCameras, fakeFactory(), {}, out);
    ASSERT_FALSE(a.has_value());
    EXPECT_EQ(a.error().code, Errc::InvalidArgument);

    RecordPlan noRoot = makeTestPlan();
    noRoot.outDir.clear();
    const auto b = runRecording(noRoot, fakeFactory(), {}, out);
    ASSERT_FALSE(b.has_value());
    EXPECT_EQ(b.error().code, Errc::InvalidArgument);

    const auto c = runRecording(makeTestPlan(), camera::CameraFactory{}, {}, out);
    ASSERT_FALSE(c.has_value());
    EXPECT_EQ(c.error().code, Errc::InvalidArgument);
}

TEST_F(RecordSessionTest, StopsWhenEveryCameraReachedTheFrameLimit) {
    RecordPlan plan = makeTestPlan();
    plan.framesPerCamera = 20;

    std::ostringstream out;
    const auto report = runRecording(plan, fakeFactory(), {}, out);
    ASSERT_TRUE(report.has_value()) << report.error().what();
    EXPECT_TRUE(report->clean());
    ASSERT_EQ(report->cameras.size(), 2U);
    for (const auto& cam : report->cameras) {
        EXPECT_GE(cam.health.framesReceived, 20U);
    }
    EXPECT_GE(report->stats.written, 40U);

    const auto session = nlohmann::json::parse(readFile(report->sessionDir / "session.json"));
    EXPECT_EQ(session["status"], "complete");
    EXPECT_EQ(session["frames_written"], report->stats.written);
    ASSERT_EQ(session["cameras"].size(), 2U);
    EXPECT_EQ(session["cameras"][0]["serial"], "SN0");
    EXPECT_EQ(session["cameras"][0]["model"], "FAKE");
    EXPECT_EQ(session["cameras"][1]["index"], 1);
    const std::string label = session["label"];
    EXPECT_NE(label.find("unit test"), std::string::npos);
    EXPECT_NE(label.find("cam0: exposure 5000 us"), std::string::npos);
    EXPECT_NE(label.find("cam1: exposure 750 us, gain 10 dB"), std::string::npos);
    EXPECT_NE(label.find("trigger hardware"), std::string::npos);
    EXPECT_TRUE(fs::exists(report->sessionDir / "cam00.vrec"));
    EXPECT_TRUE(fs::exists(report->sessionDir / "cam01.vrec"));
}

TEST_F(RecordSessionTest, StopsAfterTheDuration) {
    RecordPlan plan = makeTestPlan();
    plan.duration = 150ms;

    std::ostringstream out;
    const auto report = runRecording(plan, fakeFactory(), {}, out);
    ASSERT_TRUE(report.has_value()) << report.error().what();
    EXPECT_GE(report->elapsed, 150ms);
    EXPECT_GT(report->stats.written, 0U);
}

TEST_F(RecordSessionTest, StopsWhenAskedTo) {
    std::atomic<int> polls{0};
    std::ostringstream out;
    const auto report =
        runRecording(makeTestPlan(), fakeFactory(), [&polls] { return ++polls > 5; }, out);
    ASSERT_TRUE(report.has_value()) << report.error().what();
    EXPECT_GT(polls.load(), 5);
    EXPECT_NE(out.str().find("recording to"), std::string::npos);
}

TEST_F(RecordSessionTest, MissingCameraFailsBeforeAnySessionExists) {
    RecordPlan plan = makeTestPlan();
    plan.cameras.push_back(CameraSpec{.index = 2, .serial = std::string{kMissingSerial}});

    std::ostringstream out;
    const auto report = runRecording(plan, fakeFactory(), {}, out);
    ASSERT_FALSE(report.has_value());
    EXPECT_NE(report.error().message.find("MISSING"), std::string::npos);
    EXPECT_FALSE(fs::exists(root_)); // no session folder was created
}

TEST_F(RecordSessionTest, LoadsTheSavedSettingsOfAServiceRoot) {
    const fs::path configDir = root_ / "config"; // the rooted layout of vsort_service --root
    fs::create_directories(configDir);
    {
        MessageBus bus;
        FileConfigStore store{configDir, bus};
        ASSERT_TRUE(camera::registerCameraSettings(store).has_value());
        camera::CameraSettings settings;
        settings.exposureUs = 750.0;
        settings.gainDb = 15.0;
        ASSERT_TRUE(camera::saveCameraSettings(store, 1, settings, "test").has_value());
    }
    const auto saved = loadSavedSettings(root_);
    ASSERT_TRUE(saved.has_value()) << saved.error().what();
    ASSERT_EQ(saved->size(), 1U);
    EXPECT_DOUBLE_EQ(saved->at(1).gainDb, 15.0);
}

TEST_F(RecordSessionTest, LoadingFailsWithoutAConfigFolder) {
    const auto saved = loadSavedSettings(root_ / "nothing");
    ASSERT_FALSE(saved.has_value());
    EXPECT_EQ(saved.error().code, Errc::NotFound);
    EXPECT_FALSE(fs::exists(root_)); // nothing was created
}

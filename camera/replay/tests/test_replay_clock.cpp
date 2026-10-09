// REPLAY ONLY: the shared session clock (see replay_clock.hpp).

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <vsort/common/timestamp.hpp>
#include <vsort/recorder/recorder.hpp>
#include <vsort/replay/replay_camera.hpp>
#include <vsort/replay/replay_clock.hpp>

namespace {

using namespace vsort;
using namespace vsort::replay;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

constexpr std::uint32_t kWidth = 8;
constexpr std::uint32_t kHeight = 4;

camera::Frame makeFrame(std::uint16_t cameraIndex, std::uint64_t id, std::int64_t hostNs) {
    auto pixels = std::make_shared<std::vector<std::byte>>(
        std::size_t{kWidth} * kHeight, std::byte{static_cast<unsigned char>(id)});
    camera::Frame frame;
    frame.meta = camera::FrameMetadata{.frameId = FrameId{id},
                                       .hostTimestamp = Timestamp{std::chrono::nanoseconds{hostNs}},
                                       .deviceTimestampNs = id * 10,
                                       .width = kWidth,
                                       .height = kHeight,
                                       .strideBytes = kWidth,
                                       .pixelFormat = camera::PixelFormat::Mono8,
                                       .cameraIndex = cameraIndex};
    frame.data = std::span<const std::byte>{*pixels};
    frame.owner = pixels;
    return frame;
}

class Collector {
public:
    camera::FrameCallback callback() {
        return [this](const camera::Frame& frame) {
            {
                std::scoped_lock lock{mutex_};
                metas_.push_back(frame.meta);
            }
            changed_.notify_all();
        };
    }

    bool waitForCount(std::size_t count, std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, timeout, [&] { return metas_.size() >= count; });
    }

    std::vector<camera::FrameMetadata> metas() const {
        std::scoped_lock lock{mutex_};
        return metas_;
    }

    std::vector<std::uint64_t> ids() const {
        std::vector<std::uint64_t> out;
        for (const auto& m : metas()) {
            out.push_back(m.frameId.value());
        }
        return out;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<camera::FrameMetadata> metas_;
};

class ReplayClockTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("vsort_replay_clock_test_" + std::to_string(WallTime::now().ns()));
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    // One session. Camera i (serial "SN<i>") gets frame ID k + 1 at host time timesMs[i][k].
    fs::path record(const std::vector<std::vector<std::int64_t>>& timesMs) {
        std::vector<recorder::CameraStream> streams;
        for (std::size_t i = 0; i < timesMs.size(); ++i) {
            streams.push_back(recorder::CameraStream{.cameraIndex = static_cast<std::uint16_t>(i),
                                                     .serial = "SN" + std::to_string(i),
                                                     .model = "TestCam"});
        }
        recorder::RecorderConfig config;
        config.rootDir = root_;
        config.queueDepth = 256;
        auto rec = recorder::Recorder::start(config, std::move(streams));
        EXPECT_TRUE(rec.has_value());
        if (!rec) {
            return {};
        }
        for (std::size_t i = 0; i < timesMs.size(); ++i) {
            for (std::size_t k = 0; k < timesMs[i].size(); ++k) {
                (*rec)->submit(
                    makeFrame(static_cast<std::uint16_t>(i), k + 1, timesMs[i][k] * 1'000'000));
            }
        }
        EXPECT_TRUE((*rec)->finish().has_value());
        return (*rec)->sessionDir();
    }

    static ReplayConfig shared(const fs::path& dir, std::shared_ptr<ReplayClock> clock,
                               bool loop = false) {
        ReplayConfig c;
        c.sessionDir = dir;
        c.loop = loop;
        c.clock = std::move(clock);
        return c;
    }

    fs::path root_;
};

} // namespace

TEST_F(ReplayClockTest, ForSessionSpansAllCameras) {
    // cam0 starts later and stops earlier than cam1; cam2 recorded nothing.
    const fs::path dir = record({{100, 110, 120}, {50, 60, 70, 80, 90, 100, 110, 120, 130}, {}});
    const auto clock = ReplayClock::forSession(dir, 1.0);
    ASSERT_TRUE(clock.has_value());
    EXPECT_EQ((*clock)->firstNs(), 50'000'000);
    EXPECT_EQ((*clock)->loopPeriodNs(), 90'000'000); // 50..130 ms plus the 10 ms mean interval
}

TEST_F(ReplayClockTest, ForSessionRejectsBadInput) {
    EXPECT_EQ(ReplayClock::forSession(root_ / "missing", 1.0).error().code, Errc::NotFound);
    const fs::path dir = record({{0, 10}});
    EXPECT_EQ(ReplayClock::forSession(dir, 0.0).error().code, Errc::InvalidArgument);
    EXPECT_EQ(ReplayClock::forSession(dir, -1.0).error().code, Errc::InvalidArgument);
}

TEST_F(ReplayClockTest, CamerasKeepTheirRecordedOffset) {
    // cam1 was recorded 200 ms after cam0. Started together, in any order, it stays 200 ms later.
    const fs::path dir = record({{0, 100}, {200, 300}});
    const auto clock = ReplayClock::forSession(dir, 1.0);
    ASSERT_TRUE(clock.has_value());
    Collector out0;
    Collector out1;
    ReplayCamera cam0{shared(dir, *clock)};
    ReplayCamera cam1{shared(dir, *clock)};
    ASSERT_TRUE(cam0.open("SN0").has_value());
    ASSERT_TRUE(cam1.open("SN1").has_value());
    cam0.setFrameCallback(out0.callback());
    cam1.setFrameCallback(out1.callback());

    ASSERT_TRUE(cam1.start().has_value());
    ASSERT_TRUE(cam0.start().has_value()); // within the start delay: nothing is skipped
    ASSERT_TRUE(out0.waitForCount(2, 5s));
    ASSERT_TRUE(out1.waitForCount(2, 5s));
    cam0.stop();
    cam1.stop();

    EXPECT_EQ(out0.ids(), (std::vector<std::uint64_t>{1, 2}));
    EXPECT_EQ(out1.ids(), (std::vector<std::uint64_t>{1, 2}));
    const auto offset = out1.metas().front().hostTimestamp - out0.metas().front().hostTimestamp;
    EXPECT_GT(offset, 50ms);
    EXPECT_LT(offset, 350ms);
}

TEST_F(ReplayClockTest, AllCamerasLoopWithTheSessionPeriod) {
    // cam0 covers 20 ms, cam1 60 ms. Both loop every 60 + 15 ms (mean of 10 and 20 ms), so cam0
    // does not run ahead of cam1 pass by pass.
    const fs::path dir = record({{0, 10, 20}, {0, 20, 40, 60}});
    const auto clock = ReplayClock::forSession(dir, 1000.0);
    ASSERT_TRUE(clock.has_value());
    ASSERT_EQ((*clock)->loopPeriodNs(), 75'000'000);
    Collector out;
    ReplayCamera cam{shared(dir, *clock, true)};
    ASSERT_TRUE(cam.open("SN0").has_value());
    cam.setFrameCallback(out.callback());
    ASSERT_TRUE(cam.start().has_value());
    ASSERT_TRUE(out.waitForCount(6, 5s));
    cam.stop();

    const auto metas = out.metas();
    ASSERT_GE(metas.size(), 6U);
    EXPECT_EQ(metas[3].frameId.value(), 4U);                  // IDs keep increasing at the seam
    EXPECT_EQ(metas[3].deviceTimestampNs, 10U + 75'000'000U); // one session period later
}

TEST_F(ReplayClockTest, LateCameraJoinsTheRunningClock) {
    // Started after the start delay, a camera joins at the current media time: frames already
    // past are skipped (a frame-ID gap), the rest stays in step with the other cameras.
    const fs::path dir = record({{0, 1000, 2000}, {0, 1000, 2000}});
    const auto clock = ReplayClock::forSession(dir, 1.0);
    ASSERT_TRUE(clock.has_value());
    Collector out0;
    Collector out1;
    ReplayCamera cam0{shared(dir, *clock)};
    ReplayCamera cam1{shared(dir, *clock)};
    ASSERT_TRUE(cam0.open("SN0").has_value());
    ASSERT_TRUE(cam1.open("SN1").has_value());
    cam0.setFrameCallback(out0.callback());
    cam1.setFrameCallback(out1.callback());

    ASSERT_TRUE(cam0.start().has_value());
    std::this_thread::sleep_for(ReplayClock::kSessionStartDelay + 300ms); // media time ~300 ms
    ASSERT_TRUE(cam1.start().has_value());
    ASSERT_TRUE(out0.waitForCount(3, 5s));
    ASSERT_TRUE(out1.waitForCount(2, 5s));
    cam0.stop();
    cam1.stop();

    EXPECT_EQ(out0.ids(), (std::vector<std::uint64_t>{1, 2, 3}));
    EXPECT_EQ(out1.ids(), (std::vector<std::uint64_t>{2, 3}));
}

TEST_F(ReplayClockTest, StartAfterAllStoppedBeginsAgain) {
    const fs::path dir = record({{0, 10}});
    const auto clock = ReplayClock::forSession(dir, 1000.0);
    ASSERT_TRUE(clock.has_value());
    Collector out;
    ReplayCamera cam{shared(dir, *clock)};
    ASSERT_TRUE(cam.open("SN0").has_value());
    cam.setFrameCallback(out.callback());

    ASSERT_TRUE(cam.start().has_value());
    ASSERT_TRUE(out.waitForCount(2, 5s));
    cam.stop();
    ASSERT_TRUE(cam.start().has_value());
    ASSERT_TRUE(out.waitForCount(4, 5s));
    cam.stop();
    EXPECT_EQ(out.ids(), (std::vector<std::uint64_t>{1, 2, 1, 2}));
}

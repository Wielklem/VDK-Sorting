#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <vsort/common/timestamp.hpp>
#include <vsort/recorder/recorder.hpp>
#include <vsort/recorder/recording_format.hpp>
#include <vsort/replay/replay_camera.hpp>

namespace {

using namespace vsort;
using namespace vsort::replay;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

constexpr std::uint32_t kWidth = 8;
constexpr std::uint32_t kHeight = 4;
constexpr std::size_t kPayload = std::size_t{kWidth} * kHeight;

camera::Frame makeFrame(std::uint16_t cameraIndex, std::uint64_t id, std::int64_t hostNs) {
    auto pixels = std::make_shared<std::vector<std::byte>>(
        kPayload, std::byte{static_cast<unsigned char>(id)});
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

// Collects delivered frames and lets a test wait for a number of them.
class Collector {
public:
    camera::FrameCallback callback() {
        return [this](const camera::Frame& frame) {
            {
                std::scoped_lock lock{mutex_};
                metas_.push_back(frame.meta);
                firstBytes_.push_back(frame.data.empty() ? std::byte{0} : frame.data.front());
                sizes_.push_back(frame.data.size());
            }
            changed_.notify_all();
        };
    }

    bool waitForCount(std::size_t count, std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, timeout, [&] { return metas_.size() >= count; });
    }

    std::vector<std::uint64_t> ids() const {
        std::scoped_lock lock{mutex_};
        std::vector<std::uint64_t> out;
        for (const auto& m : metas_) {
            out.push_back(m.frameId.value());
        }
        return out;
    }

    std::vector<camera::FrameMetadata> metas() const {
        std::scoped_lock lock{mutex_};
        return metas_;
    }

    std::vector<std::byte> firstBytes() const {
        std::scoped_lock lock{mutex_};
        return firstBytes_;
    }

    std::vector<std::size_t> sizes() const {
        std::scoped_lock lock{mutex_};
        return sizes_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<camera::FrameMetadata> metas_;
    std::vector<std::byte> firstBytes_;
    std::vector<std::size_t> sizes_;
};

class ReplayCameraTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("vsort_replay_test_" + std::to_string(WallTime::now().ns()));
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    // Records one session with the real Recorder. Camera i has serial "SN<i>". Frame k of every
    // camera gets the ID ids[k] and the host time k * spacing.
    fs::path record(const std::vector<std::uint64_t>& ids, std::chrono::milliseconds spacing,
                    std::uint16_t cameraCount = 1) {
        std::vector<recorder::CameraStream> streams;
        for (std::uint16_t i = 0; i < cameraCount; ++i) {
            streams.push_back(recorder::CameraStream{
                .cameraIndex = i, .serial = "SN" + std::to_string(i), .model = "TestCam"});
        }
        recorder::RecorderConfig config;
        config.rootDir = root_;
        config.queueDepth = 256;
        auto rec = recorder::Recorder::start(config, std::move(streams));
        EXPECT_TRUE(rec.has_value());
        if (!rec) {
            return {};
        }
        const std::int64_t spacingNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(spacing).count();
        for (std::size_t k = 0; k < ids.size(); ++k) {
            for (std::uint16_t i = 0; i < cameraCount; ++i) {
                (*rec)->submit(makeFrame(i, ids[k], static_cast<std::int64_t>(k) * spacingNs));
            }
        }
        EXPECT_TRUE((*rec)->finish().has_value());
        return (*rec)->sessionDir();
    }

    static std::vector<std::uint64_t> sequence(std::uint64_t count) {
        std::vector<std::uint64_t> ids;
        for (std::uint64_t id = 1; id <= count; ++id) {
            ids.push_back(id);
        }
        return ids;
    }

    static ReplayConfig config(const fs::path& dir, double speed = 1000.0, bool loop = false) {
        ReplayConfig c;
        c.sessionDir = dir;
        c.speed = speed;
        c.loop = loop;
        return c;
    }

    fs::path root_;
};

TEST_F(ReplayCameraTest, OpenRejectsBadInput) {
    const fs::path dir = record(sequence(3), 1ms);

    ReplayCamera noDir{ReplayConfig{}};
    EXPECT_EQ(noDir.open("SN0").error().code, Errc::InvalidArgument);

    ReplayCamera cam{config(dir)};
    EXPECT_EQ(cam.open("").error().code, Errc::InvalidArgument);
    EXPECT_EQ(cam.open("nope").error().code, Errc::NotFound);
    EXPECT_FALSE(cam.isOpen());

    ReplayCamera noSession{config(root_ / "missing")};
    EXPECT_EQ(noSession.open("SN0").error().code, Errc::NotFound);

    ReplayCamera zeroSpeed{config(dir, 0.0)};
    EXPECT_EQ(zeroSpeed.open("SN0").error().code, Errc::InvalidArgument);
    ReplayCamera nanSpeed{config(dir, std::numeric_limits<double>::quiet_NaN())};
    EXPECT_EQ(nanSpeed.open("SN0").error().code, Errc::InvalidArgument);
}

TEST_F(ReplayCameraTest, ReportsInfoAndFrameCount) {
    ReplayCamera cam{config(record(sequence(4), 1ms))};
    EXPECT_EQ(cam.info().error().code, Errc::NotFound);
    EXPECT_EQ(cam.frameCount(), 0U);

    ASSERT_TRUE(cam.open("SN0").has_value());
    EXPECT_TRUE(cam.isOpen());
    EXPECT_EQ(cam.open("SN0").error().code, Errc::AlreadyExists);
    EXPECT_EQ(cam.frameCount(), 4U);

    const auto info = cam.info();
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->serial, "SN0");
    EXPECT_EQ(info->model, "TestCam");
    EXPECT_EQ(info->sensorWidth, kWidth);
    EXPECT_EQ(info->sensorHeight, kHeight);

    cam.close();
    EXPECT_FALSE(cam.isOpen());
    EXPECT_EQ(cam.frameCount(), 0U);
}

TEST_F(ReplayCameraTest, DeliversRecordedFramesInOrder) {
    ReplayCamera cam{config(record(sequence(5), 1ms))};
    ASSERT_TRUE(cam.open("SN0").has_value());
    Collector out;
    cam.setFrameCallback(out.callback());

    ASSERT_TRUE(cam.start().has_value());
    EXPECT_TRUE(cam.isStreaming());
    ASSERT_TRUE(cam.waitUntilFinished(5s));
    EXPECT_TRUE(cam.isStreaming()); // stays true until stop()
    cam.stop();
    EXPECT_FALSE(cam.isStreaming());

    EXPECT_EQ(out.ids(), sequence(5));
    const auto metas = out.metas();
    for (std::size_t i = 0; i < metas.size(); ++i) {
        const auto id = static_cast<std::uint64_t>(i + 1);
        EXPECT_EQ(metas[i].deviceTimestampNs, id * 10);
        EXPECT_EQ(metas[i].width, kWidth);
        EXPECT_EQ(metas[i].height, kHeight);
        EXPECT_EQ(metas[i].strideBytes, kWidth);
        EXPECT_EQ(metas[i].pixelFormat, camera::PixelFormat::Mono8);
        EXPECT_EQ(metas[i].cameraIndex, 0);
        EXPECT_EQ(out.firstBytes()[i], std::byte{static_cast<unsigned char>(id)});
        EXPECT_EQ(out.sizes()[i], kPayload);
        if (i > 0) {
            EXPECT_GE(metas[i].hostTimestamp, metas[i - 1].hostTimestamp);
        }
    }
    const auto stats = cam.stats();
    EXPECT_EQ(stats.framesDelivered, 5U);
    EXPECT_EQ(stats.framesFailed, 0U);
    EXPECT_EQ(stats.loops, 1U);
    EXPECT_TRUE(stats.finished);
}

TEST_F(ReplayCameraTest, ReplaysTheCameraWithThatSerial) {
    ReplayCamera cam{config(record(sequence(3), 1ms, 2))};
    ASSERT_TRUE(cam.open("SN1").has_value());
    Collector out;
    cam.setFrameCallback(out.callback());
    ASSERT_TRUE(cam.start().has_value());
    ASSERT_TRUE(cam.waitUntilFinished(5s));
    cam.stop();

    ASSERT_EQ(out.metas().size(), 3U);
    EXPECT_EQ(out.metas().front().cameraIndex, 1);
    EXPECT_EQ(cam.info()->serial, "SN1");
}

TEST_F(ReplayCameraTest, KeepsTheRecordedSpacing) {
    // 6 frames, 40 ms apart: the last one is due 200 ms after the first.
    ReplayCamera cam{config(record(sequence(6), 40ms), 1.0)};
    ASSERT_TRUE(cam.open("SN0").has_value());
    Collector out;
    cam.setFrameCallback(out.callback());

    const auto begin = std::chrono::steady_clock::now();
    ASSERT_TRUE(cam.start().has_value());
    ASSERT_TRUE(cam.waitUntilFinished(5s));
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    cam.stop();

    EXPECT_GE(elapsed, 190ms); // only a lower bound: CI machines can be slow
    EXPECT_EQ(out.ids().size(), 6U);
}

TEST_F(ReplayCameraTest, SpeedScalesTheSpacing) {
    // 6 frames, 100 ms apart = 500 ms of recording. At 100x it must take far less.
    ReplayCamera cam{config(record(sequence(6), 100ms), 100.0)};
    ASSERT_TRUE(cam.open("SN0").has_value());
    Collector out;
    cam.setFrameCallback(out.callback());

    const auto begin = std::chrono::steady_clock::now();
    ASSERT_TRUE(cam.start().has_value());
    ASSERT_TRUE(cam.waitUntilFinished(5s));
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 400ms);
    cam.stop();
}

TEST_F(ReplayCameraTest, SetSpeedWorksWhileStreaming) {
    // 4 frames, 500 ms apart = 1.5 s at the original rate.
    ReplayCamera cam{config(record(sequence(4), 500ms), 1.0)};
    ASSERT_TRUE(cam.open("SN0").has_value());
    Collector out;
    cam.setFrameCallback(out.callback());

    EXPECT_EQ(cam.setSpeed(0.0).error().code, Errc::InvalidArgument);
    EXPECT_DOUBLE_EQ(cam.speed(), 1.0);

    const auto begin = std::chrono::steady_clock::now();
    ASSERT_TRUE(cam.start().has_value());
    ASSERT_TRUE(out.waitForCount(1, 2s));
    ASSERT_TRUE(cam.setSpeed(1000.0).has_value()); // wakes the wait for frame 2
    EXPECT_DOUBLE_EQ(cam.speed(), 1000.0);
    ASSERT_TRUE(cam.waitUntilFinished(5s));
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 1000ms);
    cam.stop();
    EXPECT_EQ(out.ids(), sequence(4));
}

TEST_F(ReplayCameraTest, LoopKeepsFrameIdsIncreasing) {
    ReplayCamera cam{config(record(sequence(3), 1ms), 1000.0, true)};
    ASSERT_TRUE(cam.open("SN0").has_value());
    Collector out;
    cam.setFrameCallback(out.callback());

    ASSERT_TRUE(cam.start().has_value());
    ASSERT_TRUE(out.waitForCount(10, 5s));
    EXPECT_FALSE(cam.waitUntilFinished(20ms)); // looping never finishes
    cam.stop();

    const auto ids = out.ids();
    ASSERT_GE(ids.size(), 10U);
    for (std::size_t i = 0; i < 10; ++i) {
        EXPECT_EQ(ids[i], i + 1); // 1 2 3 4 5 6 ... without a jump at the seam
    }
    EXPECT_GE(cam.stats().loops, 3U);
    EXPECT_FALSE(cam.stats().finished);

    // Device time keeps running too, so a consumer never sees it go backwards.
    const auto metas = out.metas();
    for (std::size_t i = 1; i < 10; ++i) {
        EXPECT_GT(metas[i].deviceTimestampNs, metas[i - 1].deviceTimestampNs);
    }
}

TEST_F(ReplayCameraTest, GapsInRecordedIdsAreReplayed) {
    ReplayCamera cam{config(record({1, 2, 5}, 1ms), 1000.0, true)};
    ASSERT_TRUE(cam.open("SN0").has_value());
    Collector out;
    cam.setFrameCallback(out.callback());

    ASSERT_TRUE(cam.start().has_value());
    ASSERT_TRUE(out.waitForCount(6, 5s));
    cam.stop();

    const auto ids = out.ids();
    ASSERT_GE(ids.size(), 6U);
    EXPECT_EQ((std::vector<std::uint64_t>{ids.begin(), ids.begin() + 6}),
              (std::vector<std::uint64_t>{1, 2, 5, 6, 7, 10}));
}

TEST_F(ReplayCameraTest, StopIsPromptAndStartBeginsAgain) {
    // Second frame is 10 s away. stop() must not wait for it.
    ReplayCamera cam{config(record(sequence(2), 10s), 1.0)};
    ASSERT_TRUE(cam.open("SN0").has_value());
    Collector out;
    cam.setFrameCallback(out.callback());

    ASSERT_TRUE(cam.start().has_value());
    EXPECT_EQ(cam.start().error().code, Errc::AlreadyExists);
    ASSERT_TRUE(out.waitForCount(1, 2s));
    const auto begin = std::chrono::steady_clock::now();
    cam.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 2s);
    EXPECT_FALSE(cam.stats().finished);
    cam.stop(); // safe when not streaming

    ASSERT_TRUE(cam.start().has_value());
    ASSERT_TRUE(out.waitForCount(2, 2s));
    cam.stop();
    EXPECT_EQ(out.ids(), (std::vector<std::uint64_t>{1, 1}));
}

TEST_F(ReplayCameraTest, StartNeedsOpenCamera) {
    ReplayCamera cam{config(record(sequence(2), 1ms))};
    EXPECT_EQ(cam.start().error().code, Errc::NotFound);
    ASSERT_TRUE(cam.open("SN0").has_value());
    ASSERT_TRUE(cam.start().has_value());
    cam.close(); // stops streaming
    EXPECT_FALSE(cam.isStreaming());
    EXPECT_FALSE(cam.isOpen());
}

TEST_F(ReplayCameraTest, PlaysUpToAHalfWrittenLastFrame) {
    const fs::path dir = record(sequence(4), 1ms);
    const fs::path file = dir / "cam00.vrec";
    fs::resize_file(file, fs::file_size(file) - 5);

    ReplayCamera cam{config(dir)};
    ASSERT_TRUE(cam.open("SN0").has_value());
    EXPECT_EQ(cam.frameCount(), 3U);
}

TEST_F(ReplayCameraTest, RejectsDamagedFiles) {
    const fs::path dir = record(sequence(2), 1ms);
    const fs::path file = dir / "cam00.vrec";

    {
        std::fstream f{file, std::ios::in | std::ios::out | std::ios::binary};
        f.seekp(4); // format version
        const char newer[2] = {2, 0};
        f.write(newer, 2);
    }
    ReplayCamera version{config(dir)};
    EXPECT_EQ(version.open("SN0").error().code, Errc::NotSupported);

    {
        std::fstream f{file, std::ios::in | std::ios::out | std::ios::binary};
        f.seekp(0); // magic
        f.write("X", 1);
    }
    ReplayCamera magic{config(dir)};
    EXPECT_EQ(magic.open("SN0").error().code, Errc::ParseError);

    {
        std::ofstream f{dir / "session.json", std::ios::trunc};
        f << "{ not json";
    }
    ReplayCamera session{config(dir)};
    EXPECT_EQ(session.open("SN0").error().code, Errc::ParseError);
}

TEST_F(ReplayCameraTest, RejectsFileNamesOutsideTheSession) {
    const fs::path dir = record(sequence(2), 1ms);
    std::string text;
    {
        std::ifstream in{dir / "session.json"};
        std::ostringstream content;
        content << in.rdbuf();
        text = content.str();
    }
    auto json = nlohmann::json::parse(text);
    json["cameras"][0]["file"] = "../cam00.vrec";
    {
        std::ofstream out{dir / "session.json", std::ios::trunc};
        out << json.dump();
    }
    ReplayCamera cam{config(dir)};
    EXPECT_EQ(cam.open("SN0").error().code, Errc::ParseError);
}

TEST_F(ReplayCameraTest, ApplySettingsValidatesAndStores) {
    ReplayCamera cam{config(record(sequence(2), 1ms))};
    camera::CameraSettings s;
    EXPECT_EQ(cam.applySettings(s).error().code, Errc::NotFound);

    ASSERT_TRUE(cam.open("SN0").has_value());
    s.exposureUs = 0.0;
    EXPECT_EQ(cam.applySettings(s).error().code, Errc::InvalidArgument);
    s = camera::CameraSettings{};
    s.gainDb = -1.0;
    EXPECT_EQ(cam.applySettings(s).error().code, Errc::InvalidArgument);
    s = camera::CameraSettings{};
    s.roi = camera::Roi{.x = 0, .y = 0, .width = 4, .height = 0};
    EXPECT_EQ(cam.applySettings(s).error().code, Errc::InvalidArgument);
    s.roi = camera::Roi{.x = 0, .y = 0, .width = 4, .height = 2};
    EXPECT_EQ(cam.applySettings(s).error().code, Errc::NotSupported);

    s = camera::CameraSettings{};
    s.exposureUs = 5000.0;
    s.triggerMode = camera::TriggerMode::FreeRun;
    ASSERT_TRUE(cam.applySettings(s).has_value());
    const auto stored = cam.settings();
    ASSERT_TRUE(stored.has_value());
    EXPECT_DOUBLE_EQ(stored->exposureUs, 5000.0);
    EXPECT_EQ(stored->triggerMode, camera::TriggerMode::FreeRun);
}

} // namespace

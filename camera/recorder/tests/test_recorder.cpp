#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
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

namespace {

using namespace vsort;
using namespace vsort::recorder;
namespace fs = std::filesystem;

constexpr std::uint32_t kWidth = 8;
constexpr std::uint32_t kHeight = 4;
constexpr std::size_t kPayload = std::size_t{kWidth} * kHeight;

camera::Frame makeFrame(std::uint16_t cameraIndex, std::uint64_t id) {
    auto pixels = std::make_shared<std::vector<std::byte>>(
        kPayload, std::byte{static_cast<unsigned char>(id)});
    camera::Frame frame;
    frame.meta = camera::FrameMetadata{
        .frameId = FrameId{id},
        .hostTimestamp = Timestamp{std::chrono::nanoseconds{static_cast<std::int64_t>(id) * 1000}},
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

RecorderConfig makeConfig(const fs::path& root, std::string label = "", std::size_t depth = 32) {
    RecorderConfig config;
    config.rootDir = root;
    config.label = std::move(label);
    config.queueDepth = depth;
    return config;
}

std::vector<CameraStream> twoCameras() {
    return {CameraStream{.cameraIndex = 0, .serial = "SN0", .model = "M"},
            CameraStream{.cameraIndex = 1, .serial = "SN1", .model = "M"}};
}

std::string readFile(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

class RecorderTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("vsort_recorder_test_" + std::to_string(WallTime::now().ns()));
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    fs::path root_;
};

TEST(RecordingFormat, RecordHeaderRoundTripsLittleEndian) {
    const RecordHeader in{.frameId = 0x0102030405060708ULL,
                          .hostTimestampNs = -5,
                          .deviceTimestampNs = 99,
                          .width = 1920,
                          .height = 1080,
                          .strideBytes = 2048,
                          .payloadBytes = 1234,
                          .pixelFormat = 3};
    const auto bytes = encodeRecordHeader(in);
    EXPECT_EQ(bytes[0], std::byte{0x08});
    EXPECT_EQ(bytes[7], std::byte{0x01});

    const auto out = decodeRecordHeader(bytes);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->frameId, in.frameId);
    EXPECT_EQ(out->hostTimestampNs, in.hostTimestampNs);
    EXPECT_EQ(out->deviceTimestampNs, in.deviceTimestampNs);
    EXPECT_EQ(out->width, in.width);
    EXPECT_EQ(out->height, in.height);
    EXPECT_EQ(out->strideBytes, in.strideBytes);
    EXPECT_EQ(out->payloadBytes, in.payloadBytes);
    EXPECT_EQ(out->pixelFormat, in.pixelFormat);
}

TEST(RecordingFormat, FileHeaderRoundTripAndRejectsBadInput) {
    const auto bytes = encodeFileHeader(FileHeader{.cameraIndex = 7});
    const auto out = decodeFileHeader(bytes);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->version, kFormatVersion);
    EXPECT_EQ(out->cameraIndex, 7);

    auto bad = bytes;
    bad[0] = std::byte{'X'};
    EXPECT_FALSE(decodeFileHeader(bad).has_value());
    EXPECT_FALSE(decodeFileHeader(std::span<const std::byte>{bytes}.first(4)).has_value());
    EXPECT_FALSE(decodeRecordHeader(std::span<const std::byte>{bytes}.first(10)).has_value());
}

TEST_F(RecorderTest, RejectsInvalidConfig) {
    const auto noRoot = Recorder::start(RecorderConfig{}, twoCameras());
    ASSERT_FALSE(noRoot.has_value());
    EXPECT_EQ(noRoot.error().code, Errc::InvalidArgument);

    const auto noCameras = Recorder::start(makeConfig(root_), {});
    ASSERT_FALSE(noCameras.has_value());
    EXPECT_EQ(noCameras.error().code, Errc::InvalidArgument);

    auto duplicate = twoCameras();
    duplicate[1].cameraIndex = 0;
    const auto dup = Recorder::start(makeConfig(root_), duplicate);
    ASSERT_FALSE(dup.has_value());
    EXPECT_EQ(dup.error().code, Errc::InvalidArgument);

    const auto noQueue = Recorder::start(makeConfig(root_, "", 0), twoCameras());
    ASSERT_FALSE(noQueue.has_value());
    EXPECT_EQ(noQueue.error().code, Errc::InvalidArgument);
}

TEST_F(RecorderTest, WritesFramesAndSessionFile) {
    auto rec = Recorder::start(makeConfig(root_, "unit test", 64), twoCameras());
    ASSERT_TRUE(rec.has_value());
    const fs::path dir = (*rec)->sessionDir();

    // Session is marked as running until finish().
    EXPECT_EQ(nlohmann::json::parse(readFile(dir / "session.json"))["status"], "recording");

    for (std::uint64_t id = 1; id <= 5; ++id) {
        (*rec)->submit(makeFrame(0, id));
        (*rec)->submit(makeFrame(1, id));
    }
    ASSERT_TRUE((*rec)->finish().has_value());
    EXPECT_EQ((*rec)->stats().written, 10U);
    EXPECT_EQ((*rec)->stats().dropped, 0U);
    EXPECT_EQ((*rec)->stats().failed, 0U);

    for (std::uint16_t cam = 0; cam < 2; ++cam) {
        const std::string content = readFile(dir / (cam == 0 ? "cam00.vrec" : "cam01.vrec"));
        const auto bytes = std::as_bytes(std::span{content});
        const auto fileHeader = decodeFileHeader(bytes);
        ASSERT_TRUE(fileHeader.has_value());
        EXPECT_EQ(fileHeader->cameraIndex, cam);

        std::size_t offset = kFileHeaderSize;
        std::uint64_t expectedId = 1;
        while (offset < bytes.size()) {
            const auto h = decodeRecordHeader(bytes.subspan(offset));
            ASSERT_TRUE(h.has_value());
            EXPECT_EQ(h->frameId, expectedId);
            EXPECT_EQ(h->hostTimestampNs, static_cast<std::int64_t>(expectedId) * 1000);
            EXPECT_EQ(h->deviceTimestampNs, expectedId * 10);
            EXPECT_EQ(h->width, kWidth);
            EXPECT_EQ(h->height, kHeight);
            EXPECT_EQ(h->payloadBytes, kPayload);
            ASSERT_LE(offset + kRecordHeaderSize + h->payloadBytes, bytes.size());
            EXPECT_EQ(bytes[offset + kRecordHeaderSize],
                      std::byte{static_cast<unsigned char>(expectedId)});
            offset += kRecordHeaderSize + h->payloadBytes;
            ++expectedId;
        }
        EXPECT_EQ(offset, bytes.size());
        EXPECT_EQ(expectedId, 6U);
    }

    const auto session = nlohmann::json::parse(readFile(dir / "session.json"));
    EXPECT_EQ(session["status"], "complete");
    EXPECT_EQ(session["label"], "unit test");
    EXPECT_EQ(session["frames_written"], 10);
    EXPECT_EQ(session["cameras"].size(), 2U);
    EXPECT_EQ(session["cameras"][0]["serial"], "SN0");
    EXPECT_EQ(session["cameras"][1]["frames"], 5);
    EXPECT_EQ(session["cameras"][1]["first_frame_id"], 1);
    EXPECT_EQ(session["cameras"][1]["last_frame_id"], 5);
}

TEST_F(RecorderTest, CallbackForSetsCameraIndex) {
    auto rec = Recorder::start(makeConfig(root_), twoCameras());
    ASSERT_TRUE(rec.has_value());
    const auto callback = (*rec)->callbackFor(1);
    callback(makeFrame(0, 1)); // adapter reports index 0; the callback overrides it
    ASSERT_TRUE((*rec)->finish().has_value());

    const auto session = nlohmann::json::parse(readFile((*rec)->sessionDir() / "session.json"));
    EXPECT_EQ(session["cameras"][0]["frames"], 0);
    EXPECT_EQ(session["cameras"][1]["frames"], 1);
}

TEST_F(RecorderTest, CountsFailedAndDroppedFrames) {
    auto rec = Recorder::start(makeConfig(root_), twoCameras());
    ASSERT_TRUE(rec.has_value());

    (*rec)->submit(makeFrame(7, 1)); // no such camera in this session
    ASSERT_TRUE((*rec)->finish().has_value());
    EXPECT_EQ((*rec)->stats().failed, 1U);
    EXPECT_EQ((*rec)->stats().written, 0U);

    (*rec)->submit(makeFrame(0, 2)); // after finish()
    EXPECT_EQ((*rec)->stats().dropped, 1U);
}

TEST_F(RecorderTest, SessionsGetSeparateFolders) {
    auto a = Recorder::start(makeConfig(root_), twoCameras());
    auto b = Recorder::start(makeConfig(root_), twoCameras());
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_NE((*a)->sessionDir(), (*b)->sessionDir());
}

} // namespace

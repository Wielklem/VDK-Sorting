#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "capture/capture_module.hpp"
#include "ipc/command_handler.hpp"
#include "test_ipc_util.hpp"

using namespace vsort;
using namespace vsort::service;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
namespace fb = vsort::ipc::fb;
using vsort::testutil::request;

namespace {

CaptureValues values(std::uint32_t count, std::optional<double> length = std::nullopt) {
    CaptureValues v;
    v.count = count;
    v.lengthMm = length;
    return v;
}

CaptureImage makeImage(std::uint16_t sensorId, std::uint32_t seq, std::uint32_t count) {
    CaptureImage image;
    image.sensorId = sensorId;
    image.cameraId = 0;
    image.seq = seq;
    image.frameId = 100 + seq;
    image.values = values(count);
    image.photo = cv::Mat{20, 30, CV_8UC3, cv::Scalar{100, 100, 100}};
    image.overlay = cv::Mat{20, 30, CV_8UC3, cv::Scalar{200, 200, 200}};
    return image;
}

bool waitForState(const ICaptureControl& control, CaptureState state) {
    for (int i = 0; i < 500; ++i) {
        if (control.captureStatus().state == state) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

std::size_t countJpegs(const fs::path& dir) {
    std::size_t n = 0;
    for (const auto& e : fs::directory_iterator{dir}) {
        n += e.path().extension() == ".jpg" ? 1U : 0U;
    }
    return n;
}

std::size_t countLines(const fs::path& file) {
    std::ifstream in{file};
    std::string line;
    std::size_t n = 0;
    while (std::getline(in, line)) {
        ++n;
    }
    return n;
}

} // namespace

TEST(CaptureFilter, ModesSelectByCountAndSize) {
    const CaptureFilter empty{.mode = CaptureMode::NoPotato};
    EXPECT_TRUE(empty.accepts(values(0)));
    EXPECT_FALSE(empty.accepts(values(1, 90.0)));

    const CaptureFilter single{.mode = CaptureMode::Single};
    EXPECT_FALSE(single.accepts(values(0)));
    EXPECT_TRUE(single.accepts(values(1, 90.0)));
    EXPECT_FALSE(single.accepts(values(2, 90.0)));

    const CaptureFilter multiple{.mode = CaptureMode::Multiple};
    EXPECT_FALSE(multiple.accepts(values(1, 90.0)));
    EXPECT_TRUE(multiple.accepts(values(2, 90.0)));
    EXPECT_TRUE(multiple.accepts(values(3, 90.0)));

    const CaptureFilter range{.mode = CaptureMode::SizeRange, .minMm = 80.0, .maxMm = 100.0};
    EXPECT_FALSE(range.accepts(values(0)));
    EXPECT_FALSE(range.accepts(values(1, 79.9)));
    EXPECT_TRUE(range.accepts(values(1, 80.0)));
    EXPECT_TRUE(range.accepts(values(2, 100.0)));
    EXPECT_FALSE(range.accepts(values(1, 100.1)));
    EXPECT_FALSE(range.accepts(values(1))); // no length measured
}

TEST(CaptureFilter, ValidateRejectsBadSizeRanges) {
    EXPECT_TRUE((CaptureFilter{.mode = CaptureMode::Single}).validate().has_value());
    EXPECT_TRUE((CaptureFilter{.mode = CaptureMode::SizeRange, .minMm = 0.0, .maxMm = 0.0})
                    .validate()
                    .has_value());
    EXPECT_FALSE((CaptureFilter{.mode = CaptureMode::SizeRange, .minMm = 90.0, .maxMm = 80.0})
                     .validate()
                     .has_value());
    EXPECT_FALSE((CaptureFilter{.mode = CaptureMode::SizeRange, .minMm = -1.0, .maxMm = 80.0})
                     .validate()
                     .has_value());
    EXPECT_FALSE((CaptureFilter{.mode = CaptureMode::SizeRange,
                                .minMm = 1.0,
                                .maxMm = std::numeric_limits<double>::infinity()})
                     .validate()
                     .has_value());
    EXPECT_FALSE((CaptureFilter{.mode = static_cast<CaptureMode>(9)}).validate().has_value());
}

TEST(CaptureValuesTest, ReadFromMeasurementKeys) {
    const auto v = makeCaptureValues({{"count", 2.0},
                                      {"present", 1.0},
                                      {"mask_pct", 12.0},
                                      {"length_mm", 80.0},
                                      {"width_mm", 50.0},
                                      {"area_mm2", 3000.0}});
    EXPECT_EQ(v.count, 2U);
    EXPECT_DOUBLE_EQ(v.lengthMm.value_or(-1.0), 80.0);
    EXPECT_DOUBLE_EQ(v.widthMm.value_or(-1.0), 50.0);
    EXPECT_DOUBLE_EQ(v.areaMm2.value_or(-1.0), 3000.0);

    const auto none = makeCaptureValues({{"count", 0.0}, {"mask_pct", 1.0}});
    EXPECT_EQ(none.count, 0U);
    EXPECT_FALSE(none.lengthMm.has_value());
    EXPECT_EQ(verdictName(0), "empty");
    EXPECT_EQ(verdictName(1), "single");
    EXPECT_EQ(verdictName(4), "multi");
}

TEST(CaptureUtf8, PathsRoundTrip) {
    const fs::path path = fs::path{"photos"} / fs::path{std::u8string{u8"k\u00e4se"}};
    EXPECT_EQ(toUtf8(pathFromUtf8(toUtf8(path))), toUtf8(path));
    EXPECT_EQ(toUtf8(path).find("k\xC3\xA4se") != std::string::npos, true);
}

class CaptureModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    void TearDown() override {
        module.stop();
        fs::remove_all(dir);
    }

    [[nodiscard]] CaptureRequest makeRequest(std::uint32_t photos = 2,
                                             std::vector<std::uint16_t> sensors = {1}) const {
        return CaptureRequest{.folder = dir / "out",
                              .photosPerSensor = photos,
                              .filter = {.mode = CaptureMode::NoPotato},
                              .sensorIds = std::move(sensors)};
    }

    fs::path dir = fs::temp_directory_path() / "vsort_capture_test";
    CaptureModule module{{{.sensorId = 1, .cameraId = 0},
                          {.sensorId = 2, .cameraId = 0},
                          {.sensorId = 3, .cameraId = 1}},
                         {.defaultFolder = dir / "default", .minFreeBytes = 0}};
};

TEST_F(CaptureModuleTest, StartRejectsBadRequests) {
    auto relative = makeRequest();
    relative.folder = "relative/folder";
    auto noPhotos = makeRequest(0);
    auto noSensors = makeRequest(2, {});
    auto unknownSensor = makeRequest(2, {1, 9});
    auto badRange = makeRequest();
    badRange.filter = {.mode = CaptureMode::SizeRange, .minMm = 90.0, .maxMm = 80.0};
    for (const auto& bad : {relative, noPhotos, noSensors, unknownSensor, badRange}) {
        const auto r = module.startCapture(bad);
        ASSERT_FALSE(r.has_value());
        EXPECT_EQ(r.error().code, Errc::InvalidArgument);
    }
    EXPECT_EQ(module.captureStatus().state, CaptureState::Idle);
    EXPECT_FALSE(fs::exists(dir / "out"));
}

TEST_F(CaptureModuleTest, StatusBeforeTheFirstStartListsEverySensor) {
    const auto status = module.captureStatus();
    EXPECT_EQ(status.state, CaptureState::Idle);
    EXPECT_TRUE(status.sessionFolder.empty());
    EXPECT_EQ(status.defaultFolder, dir / "default");
    ASSERT_EQ(status.sensors.size(), 3U);
    EXPECT_EQ(status.sensors[2].sensorId, 3);
    EXPECT_EQ(status.sensors[2].cameraId, 1);
    EXPECT_FALSE(status.sensors[2].selected);
    EXPECT_FALSE(module.reserve(1, values(0))); // nothing is running
}

TEST_F(CaptureModuleTest, SavesMatchingPhotosThenIsDone) {
    const auto started = module.startCapture(makeRequest(2, {1}));
    ASSERT_TRUE(started.has_value()) << started.error().what();
    EXPECT_EQ(started->state, CaptureState::Running);

    EXPECT_FALSE(module.reserve(1, values(1, 90.0))); // the filter wants empty cups
    EXPECT_FALSE(module.reserve(2, values(0)));       // sensor 2 is not selected
    const auto first = module.reserve(1, values(0));
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, 1U);
    module.commit(makeImage(1, *first, 0));
    const auto second = module.reserve(1, values(0));
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, 2U);
    module.commit(makeImage(1, *second, 0));
    EXPECT_FALSE(module.reserve(1, values(0))); // quota full

    ASSERT_TRUE(waitForState(module, CaptureState::Done));
    const auto status = module.captureStatus();
    EXPECT_TRUE(status.sensors[0].selected);
    EXPECT_EQ(status.sensors[0].saved, 2U);
    EXPECT_EQ(status.sensors[0].target, 2U);
    EXPECT_FALSE(status.sensors[1].selected);
    EXPECT_EQ(status.sensors[1].target, 0U);

    const fs::path sensorDir = status.sessionFolder / "sensor_1";
    EXPECT_EQ(countJpegs(sensorDir), 4U); // 2 photos + 2 overlays
    EXPECT_TRUE(fs::exists(sensorDir / "0001_empty_n0_f101.jpg"));
    EXPECT_TRUE(fs::exists(sensorDir / "0001_empty_n0_f101_overlay.jpg"));
    EXPECT_GT(fs::file_size(sensorDir / "0002_empty_n0_f102.jpg"), 0U);
    EXPECT_EQ(countLines(status.sessionFolder / "captures.csv"), 3U); // header + 2
    EXPECT_FALSE(fs::exists(status.sessionFolder / "sensor_2"));
}

TEST_F(CaptureModuleTest, SizeRangeNeedsAnObjectWithinTheRange) {
    auto req = makeRequest(5, {1});
    req.filter = {.mode = CaptureMode::SizeRange, .minMm = 100.0, .maxMm = 120.0};
    ASSERT_TRUE(module.startCapture(req).has_value());
    EXPECT_FALSE(module.reserve(1, values(1, 90.0)));
    EXPECT_FALSE(module.reserve(1, values(0)));
    EXPECT_FALSE(module.reserve(1, values(2, 121.0)));
    EXPECT_TRUE(module.reserve(1, values(1, 110.0)).has_value());
    module.cancel(1);
}

TEST_F(CaptureModuleTest, StopWritesWhatIsQueuedAndEndsTheCapture) {
    ASSERT_TRUE(module.startCapture(makeRequest(10, {1})).has_value());
    const auto first = module.reserve(1, values(0));
    ASSERT_TRUE(first.has_value());
    module.commit(makeImage(1, *first, 0));

    const auto stopped = module.stopCapture();
    EXPECT_EQ(stopped.state, CaptureState::Stopped);
    EXPECT_EQ(stopped.sensors[0].saved, 1U);
    EXPECT_FALSE(module.reserve(1, values(0)));                   // nothing is wanted any more
    EXPECT_EQ(module.stopCapture().state, CaptureState::Stopped); // stopping twice is fine
}

TEST_F(CaptureModuleTest, SecondStartWhileRunningIsRejected) {
    ASSERT_TRUE(module.startCapture(makeRequest()).has_value());
    const auto again = module.startCapture(makeRequest());
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, Errc::AlreadyExists);
}

TEST_F(CaptureModuleTest, CancelGivesThePhotoNumberBack) {
    ASSERT_TRUE(module.startCapture(makeRequest(1, {1})).has_value());
    const auto first = module.reserve(1, values(0));
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(module.reserve(1, values(0))); // quota is 1 and reserved
    module.cancel(1);
    EXPECT_EQ(module.reserve(1, values(0)).value_or(0U), 1U);
}

TEST_F(CaptureModuleTest, NewSessionAfterDoneStartsFresh) {
    ASSERT_TRUE(module.startCapture(makeRequest(1, {1})).has_value());
    module.commit(makeImage(1, module.reserve(1, values(0)).value_or(0U), 0));
    ASSERT_TRUE(waitForState(module, CaptureState::Done));
    const auto firstSession = module.captureStatus().sessionFolder;

    const auto second = module.startCapture(makeRequest(1, {1, 3}));
    ASSERT_TRUE(second.has_value()) << second.error().what();
    EXPECT_EQ(second->state, CaptureState::Running);
    EXPECT_NE(second->sessionFolder, firstSession);
    EXPECT_EQ(second->sensors[0].saved, 0U);
    EXPECT_TRUE(second->sensors[2].selected);
}

TEST_F(CaptureModuleTest, FolderThatCannotBeCreatedIsAnIoError) {
    fs::create_directories(dir);
    std::ofstream{dir / "file"} << "x";
    auto req = makeRequest();
    req.folder = dir / "file" / "sub"; // below a regular file
    const auto r = module.startCapture(req);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Errc::IoError);
    EXPECT_EQ(module.captureStatus().state, CaptureState::Idle);
}

TEST_F(CaptureModuleTest, ReleasesTheSessionFilesWhenTheCaptureIsOver) {
    ASSERT_TRUE(module.startCapture(makeRequest(1, {1})).has_value());
    module.commit(makeImage(1, module.reserve(1, values(0)).value_or(0U), 0));
    ASSERT_TRUE(waitForState(module, CaptureState::Done));
    const auto csv = module.captureStatus().sessionFolder / "captures.csv";
    bool removed = false; // Windows cannot delete a file that is still open
    for (int i = 0; i < 200 && !removed; ++i) {
        std::error_code ec;
        removed = fs::remove(csv, ec) && !ec;
        if (!removed) {
            std::this_thread::sleep_for(10ms);
        }
    }
    EXPECT_TRUE(removed);
}

TEST_F(CaptureModuleTest, TooLittleFreeSpaceIsAnIoError) {
    CaptureModule full{
        {{.sensorId = 1, .cameraId = 0}},
        {.defaultFolder = dir, .minFreeBytes = std::numeric_limits<std::uint64_t>::max()}};
    const auto r = full.startCapture(makeRequest());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Errc::IoError);
}

// ---- IPC: start, status, stop through the command handler ----

namespace {

using Bytes = std::vector<std::uint8_t>;

struct Reply {
    Bytes bytes;
    const fb::Envelope* env{nullptr};
};

Reply parse(Bytes bytes) {
    Reply r;
    r.bytes = std::move(bytes);
    const auto env = ipc::parseEnvelope(r.bytes);
    EXPECT_TRUE(env.has_value());
    r.env = env.value_or(nullptr);
    return r;
}

class CaptureCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    void TearDown() override {
        module.stop();
        fs::remove_all(dir);
    }

    Reply call(const Bytes& req) { return parse(handler.handle(req)); }

    Reply start(fb::CaptureMode mode, std::uint32_t photos, const std::string& folder,
                const std::vector<std::uint16_t>& ids) {
        flatbuffers::FlatBufferBuilder fbb;
        const auto path = fbb.CreateString(folder);
        const auto sensors = fbb.CreateVector(ids);
        const auto p = fb::CreateStartCaptureRequest(fbb, path, photos, mode, 80.0, 120.0, sensors);
        return call(request(fbb, fb::MsgType::StartCapture, 21, fb::Payload::StartCaptureRequest,
                            p.Union()));
    }
    Reply status() {
        flatbuffers::FlatBufferBuilder fbb;
        const auto p = fb::CreateGetCaptureStatusRequest(fbb);
        return call(request(fbb, fb::MsgType::GetCaptureStatus, 22,
                            fb::Payload::GetCaptureStatusRequest, p.Union()));
    }
    Reply stop() {
        flatbuffers::FlatBufferBuilder fbb;
        const auto p = fb::CreateStopCaptureRequest(fbb);
        return call(
            request(fbb, fb::MsgType::StopCapture, 23, fb::Payload::StopCaptureRequest, p.Union()));
    }

    fs::path dir = fs::temp_directory_path() / "vsort_capture_cmd_test";
    PreviewHub hub{PreviewDefaults{}};
    testutil::FakeCameras cameras;
    CommandHandler handler{hub, cameras, nullptr, "9.9.9"};
    CaptureModule module{{{.sensorId = 1, .cameraId = 0}, {.sensorId = 2, .cameraId = 1}},
                         {.defaultFolder = dir / "default", .minFreeBytes = 0}};
};

} // namespace

TEST_F(CaptureCommandTest, NotSupportedWithoutACaptureControl) {
    const auto r = status();
    EXPECT_EQ(r.env->status(), static_cast<std::uint16_t>(Errc::NotSupported));
    const auto s = start(fb::CaptureMode::NoPotato, 5, (dir / "out").string(), {1});
    EXPECT_EQ(s.env->status(), static_cast<std::uint16_t>(Errc::NotSupported));
}

TEST_F(CaptureCommandTest, StatusShowsTheSensorsAndTheDefaultFolder) {
    handler.setCaptureControl(&module);
    const auto r = status();
    ASSERT_EQ(r.env->status(), 0U);
    const auto* reply = r.env->payload_as_CaptureStatusReply();
    ASSERT_NE(reply, nullptr);
    EXPECT_EQ(reply->state(), fb::CaptureState::Idle);
    EXPECT_EQ(reply->default_folder()->str(), toUtf8(dir / "default"));
    ASSERT_NE(reply->sensors(), nullptr);
    ASSERT_EQ(reply->sensors()->size(), 2U);
    EXPECT_EQ(reply->sensors()->Get(1)->camera_id(), 1);
}

TEST_F(CaptureCommandTest, StartCaptureAndStopRoundTrip) {
    handler.setCaptureControl(&module);
    const auto started = start(fb::CaptureMode::Single, 1, toUtf8(dir / "out"), {2});
    ASSERT_EQ(started.env->status(), 0U) << started.env->error_text()->str();
    const auto* reply = started.env->payload_as_CaptureStatusReply();
    ASSERT_NE(reply, nullptr);
    EXPECT_EQ(reply->state(), fb::CaptureState::Running);
    EXPECT_FALSE(reply->session_folder()->str().empty());
    EXPECT_TRUE(reply->sensors()->Get(1)->selected());
    EXPECT_EQ(reply->sensors()->Get(1)->target(), 1U);
    EXPECT_FALSE(reply->sensors()->Get(0)->selected());

    const auto seq = module.reserve(2, values(1, 90.0));
    ASSERT_TRUE(seq.has_value());
    module.commit(makeImage(2, *seq, 1));
    ASSERT_TRUE(waitForState(module, CaptureState::Done));
    const auto done = status();
    EXPECT_EQ(done.env->payload_as_CaptureStatusReply()->state(), fb::CaptureState::Done);
    EXPECT_EQ(done.env->payload_as_CaptureStatusReply()->sensors()->Get(1)->saved(), 1U);

    const auto stopped = stop(); // stopping a finished capture keeps its result
    EXPECT_EQ(stopped.env->payload_as_CaptureStatusReply()->state(), fb::CaptureState::Done);
}

TEST_F(CaptureCommandTest, BadRequestsAreAnsweredWithAnError) {
    handler.setCaptureControl(&module);
    const auto badMode = start(static_cast<fb::CaptureMode>(9), 5, toUtf8(dir / "out"), {1});
    EXPECT_EQ(badMode.env->status(), static_cast<std::uint16_t>(Errc::InvalidArgument));
    const auto relative = start(fb::CaptureMode::NoPotato, 5, "relative/folder", {1});
    EXPECT_EQ(relative.env->status(), static_cast<std::uint16_t>(Errc::InvalidArgument));
    const auto noSensors = start(fb::CaptureMode::NoPotato, 5, toUtf8(dir / "out"), {});
    EXPECT_EQ(noSensors.env->status(), static_cast<std::uint16_t>(Errc::InvalidArgument));
    ASSERT_TRUE(
        module
            .startCapture(
                {.folder = dir / "out", .photosPerSensor = 1, .filter = {}, .sensorIds = {1}})
            .has_value());
    const auto again = start(fb::CaptureMode::NoPotato, 5, toUtf8(dir / "out"), {1});
    EXPECT_EQ(again.env->status(), static_cast<std::uint16_t>(Errc::AlreadyExists));
}

TEST_F(CaptureCommandTest, HelloListsTheCaptureCapabilityOnlyWithAControl) {
    flatbuffers::FlatBufferBuilder fbb;
    const auto p = fb::CreateHelloRequest(fbb, ipc::kProtocolVersion);
    const auto bytes = request(fbb, fb::MsgType::Hello, 11, fb::Payload::HelloRequest, p.Union());
    const auto hasCapture = [](const Reply& r) {
        for (const auto* cap : *r.env->payload_as_HelloReply()->capabilities()) {
            if (cap->str() == "capture") {
                return true;
            }
        }
        return false;
    };
    EXPECT_FALSE(hasCapture(call(bytes)));
    handler.setCaptureControl(&module);
    EXPECT_TRUE(hasCapture(call(bytes)));
}

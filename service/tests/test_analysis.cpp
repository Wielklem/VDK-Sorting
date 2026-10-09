#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>

#include <vsort/common/machine_config.hpp>
#include <vsort/common/roi_config.hpp>

#include "analysis/analysis_config.hpp"
#include "analysis/analysis_module.hpp"
#include "analysis/analysis_overlay.hpp"
#include "analysis/camera_rates.hpp"
#include "analysis/pipeline.hpp"
#include "analysis/result_join.hpp"
#include "ipc/overlay_messages.hpp"
#include "test_ipc_util.hpp"

using namespace vsort;
using namespace vsort::service;
using namespace std::chrono_literals;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Dark background with white ellipses (centre, half axes, angle in degrees), BGR. The tests use
// their own HSV range (bright pixels) instead of the eqraftvision defaults.
struct Ellipse {
    cv::Point centre;
    cv::Size halfAxes;
    double angle{0.0};
};

std::shared_ptr<testutil::TestFrame> ellipseFrame(std::uint16_t cameraId, std::uint64_t frameId,
                                                  const std::vector<Ellipse>& ellipses,
                                                  int width = 640, int height = 480) {
    auto f = testutil::makeFrame(cameraId, frameId, static_cast<std::uint32_t>(width),
                                 static_cast<std::uint32_t>(height), 30, camera::PixelFormat::Bgr8);
    cv::Mat img{height, width, CV_8UC3, f->bytes.data()};
    for (const auto& e : ellipses) {
        cv::ellipse(img, e.centre, e.halfAxes, e.angle, 0.0, 360.0, cv::Scalar{230, 230, 230},
                    cv::FILLED);
    }
    return f;
}

AnalysisParams testParams() {
    AnalysisParams p;
    p.hsvLower = {0, 0, 120};
    p.hsvUpper = {179, 255, 255};
    p.roiWidthMm = 0.0; // tests set the scale themselves
    p.mmPerPx = 1.0;
    p.minDiameterPx = 40.0;
    p.minHullAreaPx = 1000.0;
    return p;
}

double value(const std::vector<MeasurementValue>& values, std::string_view key) {
    for (const auto& v : values) {
        if (v.key == key) {
            return v.value;
        }
    }
    return -1.0;
}

bool has(const std::vector<MeasurementValue>& values, std::string_view key) {
    return value(values, key) >= 0.0;
}

// Whole frame as lane ROI, no buffer.
Result<AnalysisContext> runOn(const camera::Frame& frame, const AnalysisParams& params,
                              const camera::Roi& lane) {
    const auto region =
        analysisRegion(lane, params.roiBufferPx, frame.meta.width, frame.meta.height, 1);
    AnalysisContext ctx;
    if (auto ok = prepareContext(frame, region, ctx); !ok) {
        return std::unexpected{ok.error()};
    }
    std::vector<StageTiming> timings;
    if (auto ok = makeCupPipeline(params).run(ctx, timings); !ok) {
        return std::unexpected{ok.error()};
    }
    EXPECT_EQ(timings.size(), 3U);
    return ctx;
}

ObjectRecord okRecord(std::uint16_t sensorId, std::uint64_t frameId, std::int64_t cupId) {
    ObjectRecord r;
    r.laneId = 1;
    r.cupId = cupId;
    r.sensorId = sensorId;
    r.cameraId = static_cast<std::uint16_t>(sensorId - 1);
    r.status = PhotoStatus::Ok;
    r.frameId = FrameId{frameId};
    return r;
}

SensorResult result(std::uint16_t sensorId, std::uint64_t frameId, double count) {
    return SensorResult{.sensorId = sensorId,
                        .frameId = FrameId{frameId},
                        .values = {{std::string{kKeyCount}, count}}};
}

} // namespace

TEST(AnalysisConfig, DefaultsRoundTripAndOverridesPerSensor) {
    const auto defaults = analysisConfigFromJson(analysisDefaults());
    ASSERT_TRUE(defaults.has_value()) << defaults.error().what();
    EXPECT_TRUE(defaults->enabled);
    EXPECT_EQ(defaults->defaults, AnalysisParams{});
    // eqraftvision values of the test set-up
    EXPECT_EQ(defaults->defaults.hsvLower, (std::array<std::uint8_t, 3>{60, 70, 0}));
    EXPECT_EQ(defaults->defaults.hsvUpper, (std::array<std::uint8_t, 3>{105, 170, 255}));
    EXPECT_DOUBLE_EQ(defaults->defaults.roiWidthMm, 200.0);
    EXPECT_DOUBLE_EQ(defaults->defaults.minHullAreaPx, 6000.0);

    AnalysisConfig c;
    c.debugEveryN = 5;
    AnalysisParams p;
    p.hsvLower = {10, 20, 30};
    p.roiWidthMm = 80.0;
    c.sensors.emplace(3, p);
    const auto back = analysisConfigFromJson(analysisConfigToJson(c));
    ASSERT_TRUE(back.has_value()) << back.error().what();
    EXPECT_EQ(back->debugEveryN, 5U);
    EXPECT_EQ(back->forSensor(3), p);
    EXPECT_EQ(back->forSensor(1), AnalysisParams{});
}

TEST(AnalysisConfig, RejectsBadHsvAndDuplicateSensors) {
    auto j = analysisDefaults();
    j["defaults"]["hsv_lower"] = {200, 0, 0}; // H above upper and above 179
    j["defaults"]["hsv_upper"] = {190, 255, 255};
    EXPECT_FALSE(analysisConfigFromJson(j).has_value());

    j = analysisDefaults();
    j["defaults"]["hsv_lower"] = {0, 0};
    EXPECT_FALSE(analysisConfigFromJson(j).has_value());

    j = analysisDefaults();
    j["sensors"] = {{{"sensor_id", 1}, {"params", j["defaults"]}},
                    {{"sensor_id", 1}, {"params", j["defaults"]}}};
    const auto dup = analysisConfigFromJson(j);
    ASSERT_FALSE(dup.has_value());
    EXPECT_EQ(dup.error().code, Errc::ValidationFailed);

    j = analysisDefaults();
    j["defaults"]["unknown"] = 1;
    EXPECT_FALSE(analysisConfigFromJson(j).has_value());
}

TEST(AnalysisRegion, GrowsClampsAndAligns) {
    const camera::Roi lane{.x = 100, .y = 10, .width = 200, .height = 100};
    const auto r =
        analysisRegion(lane, {.left = 51, .top = 50, .right = 30, .bottom = 1000}, 640, 480, 2);
    EXPECT_EQ(r.crop.x, 48U); // 100 - 51 = 49, aligned down
    EXPECT_EQ(r.crop.y, 0U);  // clamped
    EXPECT_EQ(r.crop.width % 2, 0U);
    EXPECT_EQ(r.crop.height, 480U);
    EXPECT_EQ(r.roi, (cv::Rect{52, 10, 200, 100}));
}

TEST(AnalysisPipeline, MeasuresOneObject) {
    const auto f =
        ellipseFrame(0, 1, {{.centre = {320, 240}, .halfAxes = {90, 60}, .angle = 30.0}});
    auto params = testParams();
    params.mmPerPx = 0.5;
    params.roiBufferPx = {};
    const auto ctx = runOn(f->frame, params, {.x = 0, .y = 0, .width = 640, .height = 480});
    ASSERT_TRUE(ctx.has_value()) << ctx.error().what();
    ASSERT_EQ(ctx->blobs.size(), 1U);
    EXPECT_EQ(value(ctx->values, kKeyCount), 1.0);
    EXPECT_NEAR(value(ctx->values, kKeyLength), 180.0 * 0.5, 2.0);
    EXPECT_NEAR(value(ctx->values, kKeyWidth), 120.0 * 0.5, 2.0);
    EXPECT_NEAR(value(ctx->values, kKeyArea), kPi * 90.0 * 60.0 * 0.25, 60.0);
    EXPECT_NEAR(value(ctx->values, kKeyMaskPct), 100.0 * kPi * 90 * 60 / (640.0 * 480.0), 1.0);
    EXPECT_GT(ctx->blobs[0].solidity, 0.95);
}

TEST(AnalysisPipeline, EmptyCupAndNoiseGiveCountZero) {
    const auto f = ellipseFrame(0, 1, {{.centre = {100, 100}, .halfAxes = {8, 8}}}); // speck
    const auto ctx = runOn(f->frame, testParams(), {.x = 0, .y = 0, .width = 640, .height = 480});
    ASSERT_TRUE(ctx.has_value());
    EXPECT_EQ(value(ctx->values, kKeyCount), 0.0);
    EXPECT_FALSE(has(ctx->values, kKeyLength));
}

TEST(AnalysisPipeline, ObjectCentredInTheBufferBelongsToTheNeighbour) {
    // Lane ROI is the left half; one object in it, one centred right of it but within the buffer.
    const auto f = ellipseFrame(0, 1,
                                {{.centre = {150, 240}, .halfAxes = {60, 45}},
                                 {.centre = {370, 240}, .halfAxes = {60, 45}}});
    auto params = testParams();
    params.roiBufferPx = {.left = 100, .top = 100, .right = 100, .bottom = 100};
    const auto ctx = runOn(f->frame, params, {.x = 0, .y = 0, .width = 320, .height = 480});
    ASSERT_TRUE(ctx.has_value());
    EXPECT_EQ(ctx->blobs.size(), 1U);
    EXPECT_EQ(ctx->neighbours.size(), 1U);
}

TEST(AnalysisPipeline, TwoObjectsAndRoiWidthScale) {
    const auto f = ellipseFrame(0, 1,
                                {{.centre = {200, 240}, .halfAxes = {50, 40}},
                                 {.centre = {440, 240}, .halfAxes = {80, 60}}});
    auto params = testParams();
    params.roiWidthMm = 320.0; // 640 px wide ROI -> 0.5 mm/px
    const auto ctx = runOn(f->frame, params, {.x = 0, .y = 0, .width = 640, .height = 480});
    ASSERT_TRUE(ctx.has_value());
    EXPECT_EQ(value(ctx->values, kKeyCount), 2.0);
    EXPECT_DOUBLE_EQ(ctx->mmPerPx, 0.5);
    EXPECT_NEAR(value(ctx->values, kKeyLength), 160.0 * 0.5, 2.0); // largest object first
}

TEST(AnalysisPipeline, MonoAndBayerInput) {
    for (const auto format : {camera::PixelFormat::Mono8, camera::PixelFormat::BayerRG8}) {
        auto f = testutil::makeFrame(0, 1, 640, 480, 30, format);
        cv::Mat img{480, 640, CV_8UC1, f->bytes.data()};
        cv::ellipse(img, cv::Point{320, 240}, cv::Size{90, 60}, 0.0, 0.0, 360.0, cv::Scalar{230},
                    cv::FILLED);
        const auto ctx =
            runOn(f->frame, testParams(), {.x = 0, .y = 0, .width = 640, .height = 480});
        ASSERT_TRUE(ctx.has_value()) << ctx.error().what();
        EXPECT_EQ(value(ctx->values, kKeyCount), 1.0);
    }
}

TEST(ResultJoin, EitherOrderCorrectionsAndExpiry) {
    ResultJoin join{{.resultsPerSensor = 4, .recordWait = 100ms, .maxWaiting = 16}};
    std::vector<Measurement> out;
    const auto t0 = Timestamp::now();

    join.addResult(result(1, 10, 1.0), out); // result first
    EXPECT_TRUE(out.empty());
    join.addRecord(okRecord(1, 10, 5), t0, out);
    ASSERT_EQ(out.size(), 1U);
    EXPECT_EQ(out[0].cupId, 5);
    EXPECT_EQ(out[0].sensorId, 1);
    EXPECT_EQ(out[0].frameId, FrameId{10});

    join.addRecord(okRecord(2, 11, 6), t0, out); // record first
    EXPECT_EQ(out.size(), 1U);
    join.addResult(result(2, 11, 0.0), out);
    ASSERT_EQ(out.size(), 2U);
    EXPECT_EQ(out[1].cupId, 6);

    join.addRecord(okRecord(1, 10, 4), t0, out); // correction: same frame, other cup
    ASSERT_EQ(out.size(), 3U);
    EXPECT_EQ(out[2].cupId, 4);

    auto noData = okRecord(1, 12, 7);
    noData.status = PhotoStatus::NoData;
    join.addRecord(noData, t0, out);
    EXPECT_EQ(join.waiting(), 0U);

    join.addRecord(okRecord(1, 99, 8), t0, out); // never analysed
    EXPECT_EQ(join.waiting(), 1U);
    join.expire(t0 + 50ms);
    EXPECT_EQ(join.waiting(), 1U);
    join.expire(t0 + 200ms);
    EXPECT_EQ(join.waiting(), 0U);
    EXPECT_EQ(join.expired(), 1U);
    EXPECT_EQ(join.joined(), 3U);
}

TEST(ResultJoin, KeepsOnlyTheNewestResults) {
    ResultJoin join{{.resultsPerSensor = 2, .recordWait = 1s, .maxWaiting = 16}};
    std::vector<Measurement> out;
    for (std::uint64_t f = 1; f <= 3; ++f) {
        join.addResult(result(1, f, 1.0), out);
    }
    join.addRecord(okRecord(1, 1, 0), Timestamp::now(), out); // evicted: waits
    EXPECT_TRUE(out.empty());
    join.addRecord(okRecord(1, 3, 2), Timestamp::now(), out);
    EXPECT_EQ(out.size(), 1U);
}

class AnalysisModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(dir);
        store = std::make_unique<FileConfigStore>(dir / "config", bus);
        ASSERT_TRUE(registerMachineConfig(*store).has_value());
        ASSERT_TRUE(registerRoiConfig(*store).has_value());
        ASSERT_TRUE(registerAnalysisConfig(*store).has_value());
        auto c = analysisDefaults();
        c["defaults"]["hsv_lower"] = {0, 0, 120};
        c["defaults"]["hsv_upper"] = {179, 255, 255};
        c["defaults"]["roi_width_mm"] = 0.0;
        c["defaults"]["mm_per_px"] = 1.0;
        c["defaults"]["min_diameter_px"] = 40.0;
        c["defaults"]["min_hull_area_px"] = 1000.0;
        c["debug_every_n"] = 1;
        c["debug_max_images"] = 2;
        ASSERT_TRUE(store->set(kAnalysisModule, c, "test").has_value());
    }
    void TearDown() override {
        store.reset();
        std::filesystem::remove_all(dir);
    }

    std::filesystem::path dir = std::filesystem::temp_directory_path() / "vsort_analysis_test";
    MessageBus bus;
    std::unique_ptr<FileConfigStore> store;
};

TEST_F(AnalysisModuleTest, FrameAndRecordBecomeAMeasurement) {
    AnalysisModule module{store.get(), {.logInterval = 1s, .join = {}, .debugDir = dir / "debug"}};
    EXPECT_EQ(module.name(), "analysis");
    EXPECT_EQ(module.dependencies(), std::vector<std::string>{"tracking"});
    ModuleContext context{bus};
    ASSERT_TRUE(module.init(context).has_value());
    auto measurements = bus.subscribe<Measurement>();

    const auto early = ellipseFrame(0, 1, {});
    module.submit(early->frame); // before start: ignored
    ASSERT_TRUE(module.start().has_value());

    for (std::uint64_t f = 2; f <= 4; ++f) {
        module.submit(ellipseFrame(0, f, {{.centre = {320, 240}, .halfAxes = {90, 60}}})->frame);
        bus.publish(okRecord(1, f, static_cast<std::int64_t>(f) - 2)); // sensor 1 = camera 0
    }
    module.submit(ellipseFrame(7, 1, {})->frame); // camera without a sensor: ignored

    std::vector<Measurement> got;
    for (int i = 0; i < 300 && got.size() < 3; ++i) {
        measurements->drain([&](const Measurement& m) { got.push_back(m); });
        std::this_thread::sleep_for(10ms);
    }
    module.stop();
    ASSERT_EQ(got.size(), 3U);
    EXPECT_EQ(got[0].laneId, 1);
    EXPECT_EQ(got[0].cupId, 0);
    EXPECT_EQ(got[0].cameraId, 0);
    EXPECT_EQ(value(got[0].values, kKeyCount), 1.0);
    EXPECT_NEAR(value(got[0].values, kKeyLength), 180.0, 2.0);
    EXPECT_EQ(module.framesAnalysed(), 3U);
    EXPECT_EQ(module.health().state, HealthState::Ok);

    std::size_t images = 0;
    for (const auto& e : std::filesystem::directory_iterator{dir / "debug" / "sensor_1"}) {
        images += e.path().extension() == ".jpg" ? 1U : 0U;
    }
    EXPECT_EQ(images, 2U); // debug_max_images
}

TEST_F(AnalysisModuleTest, DisabledDoesNothing) {
    auto c = store->get(kAnalysisModule).value();
    c["enabled"] = false;
    ASSERT_TRUE(store->set(kAnalysisModule, c, "test").has_value());
    AnalysisModule module{store.get()};
    ModuleContext context{bus};
    ASSERT_TRUE(module.init(context).has_value());
    ASSERT_TRUE(module.start().has_value());
    module.submit(ellipseFrame(0, 1, {})->frame);
    module.stop();
    EXPECT_EQ(module.framesAnalysed(), 0U);
    EXPECT_EQ(module.framesDropped(), 0U);
}

TEST_F(AnalysisModuleTest, PublishesIncomingAndAnalysedRatesPerCamera) {
    AnalysisOptions options;
    options.ratesInterval = 100ms;
    AnalysisModule module{store.get(), options};
    ModuleContext context{bus};
    ASSERT_TRUE(module.init(context).has_value());
    auto rates = bus.subscribe<CameraRates>();
    ASSERT_TRUE(module.start().has_value());

    for (std::uint64_t f = 1; f <= 5; ++f) {
        module.submit(ellipseFrame(0, f, {{.centre = {320, 240}, .halfAxes = {90, 60}}})->frame);
    }
    double incoming = 0.0;
    double analysed = 0.0;
    for (int i = 0; i < 300 && analysed == 0.0; ++i) { // the frames may span two intervals
        rates->drain([&](const CameraRates& r) {
            ASSERT_EQ(r.cameras.size(), 4U); // the cameras with a sensor (default machine)
            EXPECT_EQ(r.cameras[0].cameraId, 0);
            EXPECT_EQ(r.cameras[1].incomingFps, 0.0); // camera 1 got no frames
            incoming += r.cameras[0].incomingFps;
            analysed += r.cameras[0].analysedFps;
        });
        std::this_thread::sleep_for(10ms);
    }
    module.stop();
    EXPECT_GT(incoming, 0.0);
    EXPECT_GT(analysed, 0.0);
    EXPECT_LE(analysed, incoming);
}

TEST_F(AnalysisModuleTest, SavedConfigIsAppliedWithoutRestart) {
    AnalysisModule module{store.get()};
    ModuleContext context{bus};
    ASSERT_TRUE(module.init(context).has_value());
    auto measurements = bus.subscribe<Measurement>();
    ASSERT_TRUE(module.start().has_value());
    const auto next = [&] {
        std::optional<Measurement> got;
        for (int i = 0; i < 300 && !got; ++i) {
            got = measurements->tryPop();
            if (!got) {
                std::this_thread::sleep_for(10ms);
            }
        }
        return got;
    };
    const std::vector<Ellipse> egg{{.centre = {320, 240}, .halfAxes = {90, 60}}};

    const auto first = ellipseFrame(0, 1, egg);
    module.submit(first->frame);
    bus.publish(okRecord(1, 1, 1));
    auto got = next();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(value(got->values, kKeyCount), 1.0);

    auto c = store->get(kAnalysisModule).value();
    c["defaults"]["hsv_lower"] = {0, 0, 240}; // the ellipse (V 230) is now out of range
    ASSERT_TRUE(store->set(kAnalysisModule, c, "test").has_value()); // publishes ConfigChanged
    for (int i = 0; i < 300 && module.configGeneration() == 0; ++i) {
        std::this_thread::sleep_for(10ms);
    }
    ASSERT_EQ(module.configGeneration(), 1U);

    const auto second = ellipseFrame(0, 2, egg);
    module.submit(second->frame);
    bus.publish(okRecord(1, 2, 2));
    got = next();
    module.stop();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->cupId, 2);
    EXPECT_EQ(value(got->values, kKeyCount), 0.0);
    EXPECT_EQ(value(got->values, kKeyPresent), 0.0);
}

TEST(AnalysisOverlay, ObjectsInFramePixelsWithCountedFlag) {
    // Lane ROI x 200..440 of a 640 x 480 frame, detection buffer 50 px: crop starts at x 150.
    const auto f = ellipseFrame(0, 9,
                                {{.centre = {320, 240}, .halfAxes = {80, 60}},   // in the ROI
                                 {.centre = {150, 240}, .halfAxes = {40, 40}}}); // neighbour
    auto params = testParams();
    params.roiBufferPx = {.left = 50, .top = 0, .right = 50, .bottom = 0};
    const camera::Roi lane{.x = 200, .y = 0, .width = 240, .height = 480};
    const auto region = analysisRegion(lane, params.roiBufferPx, 640, 480, 1);
    AnalysisContext ctx;
    ASSERT_TRUE(prepareContext(f->frame, region, ctx).has_value());
    std::vector<StageTiming> timings;
    ASSERT_TRUE(makeCupPipeline(params).run(ctx, timings).has_value());

    const auto overlay = makeOverlay(ctx, region, lane, f->frame.meta, 3);
    EXPECT_EQ(overlay.cameraId, 0);
    EXPECT_EQ(overlay.sensorId, 3);
    EXPECT_EQ(overlay.frameId, 9U);
    EXPECT_EQ(overlay.frameWidth, 640U);
    EXPECT_EQ(overlay.lane.x, 200U);
    ASSERT_EQ(overlay.objects.size(), 2U);
    const auto& egg = overlay.objects[0];
    EXPECT_TRUE(egg.counted);
    EXPECT_FALSE(overlay.objects[1].counted);
    EXPECT_NEAR(egg.lengthMm, 160.0, 3.0); // mm_per_px 1
    ASSERT_GE(egg.contour.size(), 8U);
    ASSERT_EQ(egg.contour.size() % 2, 0U);
    int minX = 10000;
    int maxX = 0;
    for (std::size_t i = 0; i < egg.contour.size(); i += 2) {
        minX = std::min(minX, egg.contour[i]);
        maxX = std::max(maxX, egg.contour[i]);
    }
    EXPECT_NEAR(minX, 240, 3); // frame pixels, not crop pixels
    EXPECT_NEAR(maxX, 400, 3);
}

TEST_F(AnalysisModuleTest, PublishesAnOverlayPerAnalysedFrame) {
    AnalysisModule module{store.get()};
    ModuleContext context{bus};
    ASSERT_TRUE(module.init(context).has_value());
    auto overlays = bus.subscribe<AnalysisOverlay>();
    ASSERT_TRUE(module.start().has_value());
    module.submit(ellipseFrame(0, 4, {{.centre = {320, 240}, .halfAxes = {90, 60}}})->frame);
    std::optional<AnalysisOverlay> got;
    for (int i = 0; i < 300 && !got; ++i) {
        got = overlays->tryPop();
        if (!got) {
            std::this_thread::sleep_for(10ms);
        }
    }
    module.stop();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->frameId, 4U);
    EXPECT_EQ(got->sensorId, 1);
    ASSERT_EQ(got->objects.size(), 1U);
    EXPECT_TRUE(got->objects[0].counted);
}

TEST(AnalysisOverlay, EnvelopeCarriesFrameRoiAndContours) {
    AnalysisOverlay overlay{
        .cameraId = 2,
        .sensorId = 3,
        .frameId = 77,
        .frameWidth = 640,
        .frameHeight = 480,
        .lane = {.x = 10, .y = 20, .width = 300, .height = 400},
        .objects = {
            {.contour = {1, 2, 3, 4, 5, 6}, .counted = true, .lengthMm = 61.5, .widthMm = 44.0}}};
    const auto bytes = makeAnalysisOverlayEnvelope(overlay);
    const auto env = ipc::parseEnvelope(bytes);
    ASSERT_TRUE(env.has_value());
    EXPECT_EQ((*env)->msg_type(), static_cast<std::uint16_t>(ipc::fb::MsgType::AnalysisOverlay));
    const auto* e = (*env)->payload_as_AnalysisOverlayEvent();
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->camera_id(), 2);
    EXPECT_EQ(e->frame_id(), 77U);
    EXPECT_EQ(e->roi_width(), 300U);
    ASSERT_EQ(e->objects()->size(), 1U);
    EXPECT_TRUE(e->objects()->Get(0)->counted());
    EXPECT_EQ(e->objects()->Get(0)->contour()->size(), 6U);
    EXPECT_FLOAT_EQ(e->objects()->Get(0)->length_mm(), 61.5F);
}

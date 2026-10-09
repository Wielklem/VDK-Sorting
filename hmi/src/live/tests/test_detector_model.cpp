#include <QSize>
#include <QString>
#include <QVariantMap>

#include <gtest/gtest.h>

#include "fake_service.hpp"
#include "live/detector_model.hpp"
#include "live/service_client.hpp"
#include "test_util.hpp"

namespace {

using namespace vsort::hmi;
using namespace vsort::hmi::test;

AnalysisOverlayData overlay(quint16 cameraId, quint16 sensorId, quint64 frameId) {
    return AnalysisOverlayData{
        .cameraId = cameraId,
        .sensorId = sensorId,
        .frameId = frameId,
        .frameWidth = 640,
        .frameHeight = 480,
        .roi = QRect{100, 0, 300, 480},
        .objects = {
            {.contour = {10, 10, 50, 10, 50, 40},
             .counted = true,
             .lengthMm = 61.25,
             .widthMm = 44.0},
            {.contour = {0, 0, 5, 0, 5, 5}, .counted = false, .lengthMm = 0, .widthMm = 0}}};
}

class DetectorModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        client.start();
        ASSERT_TRUE(spinUntil([&] { return client.connected(); }, [this] { service.pump(); }));
    }

    // Publishes until the client got it (the SUB socket may still be joining).
    void deliver(const AnalysisOverlayData& o) {
        int seen = 0;
        const auto conn = QObject::connect(&client, &ServiceClient::analysisOverlayReceived,
                                           [&](const AnalysisOverlayData&) { ++seen; });
        ASSERT_TRUE(spinUntil([&] { return seen > 0; },
                              [&] {
                                  service.pump();
                                  service.publishAnalysisOverlay(o);
                              }));
        QObject::disconnect(conn);
    }

    FakeService service{{}};
    ServiceClient client{{.host = QStringLiteral("127.0.0.1"),
                          .commandPort = FakeService::kCommandPort,
                          .eventPort = FakeService::kEventPort}};
    DetectorModel model{client};
};

} // namespace

TEST_F(DetectorModelTest, ShowsTheObjectsOfTheShownFrameOnly) {
    model.selectCamera(3);
    model.setFrameId(7);
    EXPECT_FALSE(model.receiving());

    deliver(overlay(3, 1, 6)); // another frame: kept, not shown
    EXPECT_TRUE(model.receiving());
    EXPECT_FALSE(model.matched());
    EXPECT_TRUE(model.objects().isEmpty());

    deliver(overlay(3, 1, 7));
    ASSERT_TRUE(model.matched());
    EXPECT_EQ(model.frameSize(), QSize(640, 480));
    EXPECT_EQ(model.countedObjects(), 1);
    EXPECT_EQ(model.neighbourObjects(), 1);
    ASSERT_EQ(model.objects().size(), 2);
    const auto first = model.objects()[0].toMap();
    EXPECT_TRUE(first.value(QStringLiteral("counted")).toBool());
    EXPECT_EQ(first.value(QStringLiteral("points")).toList().size(), 6);
    EXPECT_EQ(first.value(QStringLiteral("label")).toString(), QStringLiteral("61.3 × 44.0 mm"));
    EXPECT_TRUE(model.objects()[1].toMap().value(QStringLiteral("label")).toString().isEmpty());

    model.setFrameId(6); // e.g. a frozen older picture
    EXPECT_TRUE(model.matched());
    model.setFrameId(8);
    EXPECT_FALSE(model.matched());
}

TEST_F(DetectorModelTest, OtherCamerasAreIgnoredAndSensorsAreMerged) {
    model.selectCamera(3);
    model.setFrameId(5);
    deliver(overlay(4, 9, 5)); // other camera
    EXPECT_FALSE(model.receiving());
    deliver(overlay(3, 1, 5));
    deliver(overlay(3, 2, 5)); // second lane of the same camera
    EXPECT_EQ(model.objects().size(), 4);

    model.selectCamera(4); // history is per camera
    EXPECT_FALSE(model.receiving());
    EXPECT_TRUE(model.objects().isEmpty());
}

#include <QString>
#include <cstdint>
#include <functional>

#include <gtest/gtest.h>

#include "fake_service.hpp"
#include "live/capture_model.hpp"
#include "live/service_client.hpp"
#include "test_util.hpp"

namespace {

using namespace vsort::hmi;
using namespace vsort::hmi::test;

constexpr std::uint16_t kCam = 61;

ServiceEndpoints testEndpoints() {
    return {.host = QStringLiteral("127.0.0.1"),
            .commandPort = FakeService::kCommandPort,
            .eventPort = FakeService::kEventPort};
}

class CaptureModelTest : public ::testing::Test {
protected:
    CaptureModelTest()
        : service{{kCam}}
        , client{testEndpoints()}
        , model{client, 20} {
        // Unsorted on purpose: the model orders by camera, then sensor.
        service.setCaptureSensors({{3, 1}, {1, 0}, {2, 0}});
        service.setCaptureDefaultFolder("/data/captures");
    }

    void SetUp() override {
        VSORT_HMI_SKIP_IF_NO_RINGS(service);
        client.start();
        ASSERT_TRUE(spin([&] { return client.connected(); }));
        model.refresh();
        ASSERT_TRUE(spin([&] { return model.rowCount() == 3; }));
    }

    bool spin(const std::function<bool()>& done) {
        return spinUntil(done, [this] { service.pump(); });
    }
    [[nodiscard]] QVariant role(int row, CaptureModel::Role r) const {
        return model.data(model.index(row), r);
    }

    FakeService service;
    ServiceClient client;
    CaptureModel model;
};

} // namespace

TEST_F(CaptureModelTest, ShowsTheSensorsByCameraAndProposesTheServiceFolder) {
    EXPECT_EQ(role(0, CaptureModel::SensorIdRole).toInt(), 1);
    EXPECT_EQ(role(1, CaptureModel::SensorIdRole).toInt(), 2);
    EXPECT_EQ(role(2, CaptureModel::SensorIdRole).toInt(), 3);
    EXPECT_EQ(role(2, CaptureModel::CameraIdRole).toInt(), 1);
    EXPECT_TRUE(role(0, CaptureModel::FirstOfCameraRole).toBool());
    EXPECT_FALSE(role(1, CaptureModel::FirstOfCameraRole).toBool());
    EXPECT_TRUE(role(2, CaptureModel::FirstOfCameraRole).toBool());
    for (int row = 0; row < 3; ++row) {
        EXPECT_TRUE(role(row, CaptureModel::SelectedRole).toBool());
    }
    EXPECT_EQ(model.folder().toStdString(), "/data/captures");
    EXPECT_EQ(model.photosPerSensor(), 50);
    EXPECT_EQ(model.mode(), 0);
    EXPECT_TRUE(model.canStart());
    EXPECT_EQ(model.statusText().toStdString(), "Ready");
}

TEST_F(CaptureModelTest, StartSendsTheChoicesAndFollowsTheProgress) {
    model.setPhotosPerSensor(10);
    model.setMode(3);
    model.setMinMm(80.0);
    model.setMaxMm(120.0);
    model.setSelected(1, false); // sensor 2 off
    model.start();
    ASSERT_TRUE(spin([&] { return model.running(); }));

    const auto& sent = service.lastCaptureStart();
    EXPECT_EQ(sent.folder, "/data/captures");
    EXPECT_EQ(sent.photos, 10U);
    EXPECT_EQ(sent.mode, 4); // size range
    EXPECT_DOUBLE_EQ(sent.minMm, 80.0);
    EXPECT_DOUBLE_EQ(sent.maxMm, 120.0);
    EXPECT_EQ(sent.sensorIds, (std::vector<std::uint16_t>{1, 3}));

    service.setCaptureSaved(1, 4);
    ASSERT_TRUE(spin([&] { return role(0, CaptureModel::SavedRole).toInt() == 4; }));
    EXPECT_EQ(model.statusText().toStdString(), "Capturing: 4 of 20 photos saved");

    model.setSelected(0, false); // locked while a capture runs
    EXPECT_TRUE(role(0, CaptureModel::SelectedRole).toBool());
    EXPECT_FALSE(model.canStart());

    service.setCaptureSaved(1, 10);
    service.setCaptureSaved(3, 10);
    ASSERT_TRUE(spin([&] { return !model.running(); }));
    EXPECT_EQ(model.statusText().toStdString(), "Done: 20 photos saved");
    EXPECT_EQ(model.sessionFolder().toStdString(), "/data/captures/20260101T000000");
    EXPECT_FALSE(role(1, CaptureModel::SelectedRole).toBool()); // the session's choice stays
    EXPECT_TRUE(model.canStart());
}

TEST_F(CaptureModelTest, StopEndsTheCapture) {
    model.start();
    ASSERT_TRUE(spin([&] { return model.running(); }));
    model.stop();
    ASSERT_TRUE(spin([&] { return !model.running(); }));
    EXPECT_EQ(service.captureStops(), 1);
    EXPECT_EQ(model.statusText().toStdString(), "Stopped: 0 photos saved");
}

TEST_F(CaptureModelTest, CameraToggleSelectsAllSensorsOfThatCamera) {
    model.setCameraSelected(0, false);
    EXPECT_FALSE(role(0, CaptureModel::SelectedRole).toBool());
    EXPECT_FALSE(role(1, CaptureModel::SelectedRole).toBool());
    EXPECT_TRUE(role(2, CaptureModel::SelectedRole).toBool());
    EXPECT_FALSE(role(0, CaptureModel::CameraSelectedRole).toBool());
    EXPECT_TRUE(role(2, CaptureModel::CameraSelectedRole).toBool());
    EXPECT_TRUE(model.canStart()); // sensor 3 is still on

    model.setSelected(1, true); // one sensor of camera 0 on again
    EXPECT_FALSE(role(0, CaptureModel::CameraSelectedRole).toBool());
    model.setSelected(0, true);
    EXPECT_TRUE(role(0, CaptureModel::CameraSelectedRole).toBool());

    model.selectAll(false);
    EXPECT_FALSE(model.canStart()); // nothing selected
    model.selectAll(true);
    EXPECT_TRUE(model.canStart());
}

TEST_F(CaptureModelTest, StartNeedsAFolderACountAndAValidSizeRange) {
    model.setMode(3);
    model.setMinMm(100.0);
    model.setMaxMm(50.0);
    EXPECT_FALSE(model.canStart());
    model.setMaxMm(150.0);
    EXPECT_TRUE(model.canStart());
    model.setFolder(QStringLiteral("   "));
    EXPECT_FALSE(model.canStart());
    model.setFolder(QStringLiteral("/other"));
    EXPECT_TRUE(model.canStart());
    model.setPhotosPerSensor(0); // clamped to 1
    EXPECT_EQ(model.photosPerSensor(), 1);
    model.setMode(9); // clamped to the last mode
    EXPECT_EQ(model.mode(), 3);
}

TEST_F(CaptureModelTest, ServiceErrorIsShownAndTheModelIsFreeAgain) {
    service.setCaptureStartError("less than 500 MB free in '/data/captures'");
    model.start();
    ASSERT_TRUE(spin([&] { return model.statusText().contains(QStringLiteral("500 MB")); }));
    EXPECT_FALSE(model.running());
    EXPECT_FALSE(model.busy());
    EXPECT_TRUE(model.canStart());
}

TEST(CaptureModelOffline, CannotStartWithoutTheService) {
    ServiceClient client{testEndpoints()}; // never started: no heartbeat
    CaptureModel model{client};
    model.setFolder(QStringLiteral("/data/captures"));
    EXPECT_FALSE(model.canStart());
    EXPECT_EQ(model.statusText().toStdString(), "Service offline");
    EXPECT_EQ(model.rowCount(), 0);
    model.start(); // does nothing
    EXPECT_FALSE(model.busy());
}

#include <QRectF>
#include <cstdint>

#include <gtest/gtest.h>

#include "fake_service.hpp"
#include "live/camera_settings_model.hpp"
#include "live/roi_editor_model.hpp"
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

// Editing logic only: the client is never started, so nothing is sent.
class RoiEditorModelTest : public ::testing::Test {
protected:
    void SetUp() override { model.selectCamera(kCam); }

    ServiceClient client{testEndpoints()};
    RoiEditorModel model{client};
};

TEST_F(RoiEditorModelTest, DrawnRoiIsNormalizedAndSelected) {
    ASSERT_EQ(model.addRoi(0.8, 0.9, 0.2, 0.1), 0);
    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.selected(), 0);
    EXPECT_TRUE(model.dirty());
    const QRectF rect = model.selectedRect();
    EXPECT_NEAR(rect.x(), 0.2, 1e-9);
    EXPECT_NEAR(rect.y(), 0.1, 1e-9);
    EXPECT_NEAR(rect.width(), 0.6, 1e-9);
    EXPECT_NEAR(rect.height(), 0.8, 1e-9);
}

TEST_F(RoiEditorModelTest, TooSmallRoiIsIgnored) {
    EXPECT_EQ(model.addRoi(0.1, 0.1, 0.105, 0.5), -1);
    EXPECT_EQ(model.rowCount(), 0);
    EXPECT_FALSE(model.dirty());
}

TEST_F(RoiEditorModelTest, MoveKeepsTheSizeAndStaysInsideTheImage) {
    ASSERT_EQ(model.addRoi(0.1, 0.1, 0.4, 0.5), 0); // 0.3 x 0.4
    model.moveTo(0, 0.95, -0.2);
    const QRectF rect = model.selectedRect();
    EXPECT_NEAR(rect.x(), 0.7, 1e-9);
    EXPECT_NEAR(rect.y(), 0.0, 1e-9);
    EXPECT_NEAR(rect.width(), 0.3, 1e-9);
    EXPECT_NEAR(rect.height(), 0.4, 1e-9);
}

TEST_F(RoiEditorModelTest, ResizeIsClampedToTheImageAndTheMinimumSize) {
    ASSERT_EQ(model.addRoi(0.2, 0.2, 0.4, 0.4), 0);
    model.setRect(0, 0.5, 0.5, 0.9, 0.9);
    QRectF rect = model.selectedRect();
    EXPECT_NEAR(rect.width(), 0.5, 1e-9);
    EXPECT_NEAR(rect.height(), 0.5, 1e-9);
    model.setRect(0, 0.5, 0.5, 0.0, 0.0);
    rect = model.selectedRect();
    EXPECT_NEAR(rect.width(), RoiEditorModel::kMinSize, 1e-9);
    EXPECT_NEAR(rect.height(), RoiEditorModel::kMinSize, 1e-9);
}

TEST_F(RoiEditorModelTest, RemoveFixesTheSelectionAndIdsStayUnique) {
    ASSERT_EQ(model.addRoi(0.1, 0.1, 0.2, 0.2), 0);
    ASSERT_EQ(model.addRoi(0.3, 0.3, 0.4, 0.4), 1);
    model.select(1);
    model.removeRoi(0);
    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.selected(), 0);
    ASSERT_EQ(model.addRoi(0.5, 0.5, 0.6, 0.6), 1);
    EXPECT_EQ(model.data(model.index(0), RoiEditorModel::RoiIdRole).toInt(), 2);
    EXPECT_EQ(model.data(model.index(1), RoiEditorModel::RoiIdRole).toInt(), 3);
    model.removeRoi(1);
    EXPECT_EQ(model.selected(), -1);
}

TEST_F(RoiEditorModelTest, EachCameraHasItsOwnRois) {
    ASSERT_EQ(model.addRoi(0.1, 0.1, 0.5, 0.5), 0);
    model.selectCamera(kCam + 1);
    EXPECT_EQ(model.rowCount(), 0);
    EXPECT_EQ(model.selected(), -1);
    model.selectCamera(kCam);
    EXPECT_EQ(model.rowCount(), 1);
}

class SetupModelsServiceTest : public ::testing::Test {
protected:
    SetupModelsServiceTest()
        : service{{kCam}}
        , client{testEndpoints()} {}

    void SetUp() override {
        VSORT_HMI_SKIP_IF_NO_RINGS(service);
        client.start();
    }

    void pump() { service.pump(); }

    bool waitConnected() {
        return spinUntil([this] { return client.connected(); }, [this] { pump(); });
    }

    FakeService service;
    ServiceClient client;
};

TEST_F(SetupModelsServiceTest, RoisSurviveSaveAndReload) {
    ASSERT_TRUE(waitConnected());
    RoiEditorModel writer{client};
    writer.selectCamera(kCam);
    ASSERT_EQ(writer.addRoi(0.1, 0.2, 0.5, 0.6), 0);
    writer.save();
    ASSERT_TRUE(spinUntil([&] { return writer.status().startsWith(QStringLiteral("Saved")); },
                          [this] { pump(); }));
    EXPECT_FALSE(writer.dirty());
    EXPECT_EQ(writer.rowCount(), 1); // the older background load did not wipe the edit

    RoiEditorModel reader{client};
    reader.selectCamera(kCam);
    ASSERT_TRUE(spinUntil([&] { return reader.rowCount() == 1; }, [this] { pump(); }));
    const QRectF rect = reader.index(0).isValid() ? QRectF{} : QRectF{};
    (void)rect;
    reader.select(0);
    const QRectF loaded = reader.selectedRect();
    EXPECT_NEAR(loaded.x(), 0.1, 1e-9);
    EXPECT_NEAR(loaded.y(), 0.2, 1e-9);
    EXPECT_NEAR(loaded.width(), 0.4, 1e-9);
    EXPECT_NEAR(loaded.height(), 0.4, 1e-9);
    EXPECT_FALSE(reader.dirty());
}

TEST_F(SetupModelsServiceTest, ExposureAndGainAreAppliedAndReadBack) {
    ASSERT_TRUE(waitConnected());
    CameraSettingsModel settings{client};
    settings.load(kCam);
    ASSERT_TRUE(spinUntil([&] { return settings.loaded(); }, [this] { pump(); }));
    EXPECT_NEAR(settings.exposureUs(), 5000.0, 1e-6);
    EXPECT_NEAR(settings.gainDb(), 2.0, 1e-6);

    settings.apply(8000.0, 6.5);
    ASSERT_TRUE(spinUntil([&] { return !settings.busy() && settings.exposureUs() > 7999.0; },
                          [this] { pump(); }));
    EXPECT_NEAR(settings.gainDb(), 6.5, 1e-6);
    EXPECT_NEAR(service.exposureUs(kCam), 8000.0, 1e-6);
    EXPECT_EQ(settings.status(), QStringLiteral("Applied"));
}

} // namespace

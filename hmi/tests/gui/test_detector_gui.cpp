#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <cstdint>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "fake_service.hpp"
#include "live/analysis_tuning_model.hpp"
#include "live/detector_model.hpp"
#include "live/live_view_model.hpp"
#include "live/roi_editor_model.hpp"
#include "live/service_client.hpp"
#include "test_util.hpp"

namespace {

using namespace vsort::hmi;
using namespace vsort::hmi::test;

constexpr std::uint16_t kCam = 61;

const std::string kMachine = R"({"lines": [{"id": 1, "name": "Line 1", "lanes": [
  {"id": 1, "name": "Lane 1", "sensors": [
    {"id": 1, "name": "Cam A", "kind": "camera", "camera_id": 61, "roi_id": 0,
     "offset_cups": 0, "show_in_monitor": true, "measurements": []}]}]}]})";
const std::string kAnalysis = R"({"enabled": true, "debug_every_n": 0, "debug_max_images": 200,
  "defaults": {"hsv_lower": [60, 70, 0], "hsv_upper": [105, 170, 255], "min_solidity": 0.85,
               "erode_iterations": 1}, "sensors": []})";

// Renders the real G60.30 Detector view offscreen against a fake service with one camera.
class DetectorGuiTest : public ::testing::Test {
protected:
    void SetUp() override {
        VSORT_HMI_SKIP_IF_NO_RINGS(service);
        service.setConfigJson("machine", kMachine);
        service.setConfigJson("analysis", kAnalysis);
        service.setConfigJson("rois", R"({"cameras": []})");
        QQmlComponent component{&engine, QUrl::fromLocalFile(QStringLiteral(VSORT_CAMERAS_QML_DIR
                                                                            "/DetectorView.qml"))};
        ASSERT_FALSE(component.isError()) << component.errorString().toStdString();
        page.reset(qobject_cast<QQuickItem*>(component.createWithInitialProperties(
            {{QStringLiteral("live"), QVariant::fromValue(&live)},
             {QStringLiteral("tuning"), QVariant::fromValue(&tuning)},
             {QStringLiteral("rois"), QVariant::fromValue(&rois)},
             {QStringLiteral("detector"), QVariant::fromValue(&detector)}})));
        ASSERT_NE(page, nullptr) << component.errorString().toStdString();
        window.resize(1280, 800);
        page->setParentItem(window.contentItem());
        page->setSize(QSizeF{1280, 800});
        window.show();
        client.start();
    }

    static QQuickItem* find(QQuickItem* root, const QString& name) {
        for (QQuickItem* child : root->childItems()) {
            if (child->objectName() == name) {
                return child;
            }
            if (QQuickItem* found = find(child, name)) {
                return found;
            }
        }
        return nullptr;
    }
    [[nodiscard]] QQuickItem* named(const char* name) const {
        return find(page.get(), QString::fromLatin1(name));
    }

    FakeService service{{kCam}};
    ServiceClient client{{.host = QStringLiteral("127.0.0.1"),
                          .commandPort = FakeService::kCommandPort,
                          .eventPort = FakeService::kEventPort}};
    LiveViewModel live{client};
    AnalysisTuningModel tuning{client};
    RoiEditorModel rois{client};
    DetectorModel detector{client};
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QQuickItem> page;
};

} // namespace

TEST_F(DetectorGuiTest, OutlinesOfTheShownFrameAndParameters) {
    const QQuickItem* video = nullptr;
    ASSERT_TRUE(spinUntil(
        [&] {
            video = named("video");
            return tuning.canSave() && video != nullptr &&
                   video->property("frameId").toLongLong() >= 0;
        },
        [this] {
            service.pump();
            service.writeFrames();
        }));
    live.setFrozen(kCam, true); // keep the frame (and its ID) still
    const auto frameId = static_cast<quint64>(video->property("frameId").toLongLong());
    EXPECT_EQ(detector.cameraId(), kCam);

    const AnalysisOverlayData overlay{
        .cameraId = kCam,
        .sensorId = 1,
        .frameId = frameId,
        .frameWidth = 640, // the fake preview is smaller: the view scales
        .frameHeight = 480,
        .roi = QRect{0, 0, 640, 480},
        .objects = {
            {.contour = {100, 100, 300, 100, 300, 300, 100, 300},
             .counted = true,
             .lengthMm = 60.0,
             .widthMm = 40.0},
            {.contour = {0, 0, 40, 0, 40, 40}, .counted = false, .lengthMm = 0, .widthMm = 0}}};
    ASSERT_TRUE(spinUntil([&] { return detector.matched(); },
                          [&] {
                              service.pump();
                              service.publishAnalysisOverlay(overlay);
                          }));

    const QQuickItem* layer = named("overlay");
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(layer->property("contours").toList().size(), 2);
    const QQuickItem* label = named("contourLabel");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->property("text").toString(), QStringLiteral("60.0 × 40.0 mm"));
    EXPECT_EQ(named("detectorSummary")->property("text").toString(),
              QStringLiteral("1 in the ROI, 1 neighbour"));

    QQuickItem* solidity = named("param_min_solidity");
    ASSERT_NE(solidity, nullptr);
    EXPECT_DOUBLE_EQ(solidity->property("value").toDouble(), 0.85);
    tuning.setParam(QStringLiteral("min_solidity"), 0.6);
    EXPECT_DOUBLE_EQ(solidity->property("value").toDouble(), 0.6);
    EXPECT_TRUE(named("saveButton")->property("enabled").toBool());
}

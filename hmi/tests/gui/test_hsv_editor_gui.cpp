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
#include "live/live_view_model.hpp"
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
  "defaults": {"hsv_lower": [60, 70, 0], "hsv_upper": [105, 170, 255]}, "sensors": []})";

// Renders the real G60.20 HSV editor offscreen against a fake service with one camera.
class HsvEditorGuiTest : public ::testing::Test {
protected:
    void SetUp() override {
        VSORT_HMI_SKIP_IF_NO_RINGS(service);
        service.setConfigJson("machine", kMachine);
        service.setConfigJson("analysis", kAnalysis);
        QQmlComponent component{&engine, QUrl::fromLocalFile(QStringLiteral(VSORT_CAMERAS_QML_DIR
                                                                            "/HsvEditorView.qml"))};
        ASSERT_FALSE(component.isError()) << component.errorString().toStdString();
        page.reset(qobject_cast<QQuickItem*>(component.createWithInitialProperties(
            {{QStringLiteral("live"), QVariant::fromValue(&live)},
             {QStringLiteral("tuning"), QVariant::fromValue(&tuning)}})));
        ASSERT_NE(page, nullptr) << component.errorString().toStdString();
        window.resize(1280, 800);
        page->setParentItem(window.contentItem());
        page->setSize(QSizeF{1280, 800});
        window.show();
        client.start();
    }

    void step() {
        service.pump();
        service.writeFrames();
        (void)window.grabWindow(); // renders: the HsvViewItems compute their pictures
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
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QQuickItem> page;
};

} // namespace

TEST_F(HsvEditorGuiTest, FourPicturesFollowTheRangeAndShowHistograms) {
    ASSERT_TRUE(spinUntil(
        [&] {
            const QQuickItem* h = named("hView");
            return tuning.canSave() && h != nullptr && h->property("hasFrame").toBool() &&
                   !h->property("histogram").value<QList<int>>().isEmpty();
        },
        [this] { step(); }));

    for (const char* name : {"hView", "sView", "vView", "resultView"}) {
        const QQuickItem* view = named(name);
        ASSERT_NE(view, nullptr) << name;
        EXPECT_EQ(view->property("hsvLower").value<QList<int>>(), (QList<int>{60, 70, 0})) << name;
    }
    EXPECT_EQ(named("hView")->property("histogram").value<QList<int>>().size(), 180);
    EXPECT_TRUE(named("resultView")->property("histogram").value<QList<int>>().isEmpty());

    tuning.setValue(0, 2, 40); // V min
    EXPECT_EQ(named("vView")->property("hsvLower").value<QList<int>>(), (QList<int>{60, 70, 40}));
    EXPECT_EQ(named("resultView")->property("hsvLower").value<QList<int>>(),
              (QList<int>{60, 70, 40}));
    const QQuickItem* save = named("saveButton");
    ASSERT_NE(save, nullptr);
    EXPECT_TRUE(save->property("enabled").toBool());

    tuning.setValue(0, 0, 0); // everything in range: 100 % in all channel views
    tuning.setValue(1, 0, 179);
    tuning.setValue(0, 1, 0);
    tuning.setValue(1, 1, 255);
    tuning.setValue(0, 2, 0);
    tuning.setValue(1, 2, 255);
    ASSERT_TRUE(
        spinUntil([&] { return named("resultView")->property("inRangePercent").toDouble() > 99.9; },
                  [this] { step(); }));
}

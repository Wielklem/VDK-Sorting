#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QList>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <memory>

#include <gtest/gtest.h>

#include "fake_service.hpp"
#include "live/camera_settings_model.hpp"
#include "live/live_view_model.hpp"
#include "live/roi_editor_model.hpp"
#include "live/service_client.hpp"
#include "test_util.hpp"

namespace {

using namespace vsort::hmi;
using namespace vsort::hmi::test;

constexpr std::uint16_t kCam1 = 61;
constexpr std::uint16_t kCam2 = 62;

// Renders the real G30.20 / G30.30 pages (from the source tree) offscreen against a fake service.
class SetupGuiTest : public ::testing::Test {
protected:
    SetupGuiTest()
        : service{{kCam1, kCam2}}
        , client{{.host = QStringLiteral("127.0.0.1"),
                  .commandPort = FakeService::kCommandPort,
                  .eventPort = FakeService::kEventPort}}
        , live{client}
        , roi{client}
        , settings{client} {}

    void SetUp() override {
        VSORT_HMI_SKIP_IF_NO_RINGS(service);
        window.resize(1280, 800);
        client.start();
    }

    bool loadPage(const char* file, const char* modelProperty, QObject* model) {
        QQmlComponent component{&engine, QUrl::fromLocalFile(QStringLiteral(VSORT_CAMERAS_QML_DIR) +
                                                             QStringLiteral("/") +
                                                             QString::fromLatin1(file))};
        if (component.isError()) {
            ADD_FAILURE() << component.errorString().toStdString();
            return false;
        }
        page.reset(qobject_cast<QQuickItem*>(component.createWithInitialProperties(
            {{QStringLiteral("live"), QVariant::fromValue(&live)},
             {QString::fromLatin1(modelProperty), QVariant::fromValue(model)}})));
        if (!page) {
            ADD_FAILURE() << component.errorString().toStdString();
            return false;
        }
        page->setParentItem(window.contentItem());
        page->setSize(QSizeF{1280, 800});
        window.show();
        return true;
    }

    void step() {
        service.pump();
        service.writeFrames();
    }

    static void settle() {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    static void collect(QQuickItem* root, const QString& name, QList<QQuickItem*>& out) {
        for (QQuickItem* child : root->childItems()) {
            if (child->objectName() == name) {
                out.push_back(child);
            }
            collect(child, name, out);
        }
    }

    [[nodiscard]] QList<QQuickItem*> all(const char* name) const {
        QList<QQuickItem*> result;
        collect(page.get(), QString::fromLatin1(name), result);
        return result;
    }

    [[nodiscard]] QQuickItem* named(const char* name) const {
        const auto found = all(name);
        return found.isEmpty() ? nullptr : found.first();
    }

    FakeService service;
    ServiceClient client;
    LiveViewModel live;
    RoiEditorModel roi;
    CameraSettingsModel settings;
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QQuickItem> page;
};

TEST_F(SetupGuiTest, RoiEditorShowsRoisAndHandlesOfTheSelectedCamera) {
    ASSERT_TRUE(loadPage("RoiEditorPage.qml", "roi", &roi));
    ASSERT_TRUE(spinUntil(
        [&] { return roi.cameraId() == kCam1 && named("video")->property("hasFrame").toBool(); },
        [this] { step(); }));

    ASSERT_EQ(roi.addRoi(0.1, 0.1, 0.5, 0.5), 0);
    settle();
    EXPECT_EQ(all("roi").size(), 1);       // overlay layer
    EXPECT_EQ(all("roiBox").size(), 1);    // editing layer
    EXPECT_EQ(all("roiHandle").size(), 4); // new ROI is selected

    roi.select(-1);
    settle();
    EXPECT_EQ(all("roiHandle").size(), 0);

    roi.removeRoi(0);
    settle();
    EXPECT_EQ(all("roiBox").size(), 0);
    EXPECT_EQ(all("roi").size(), 0);
}

TEST_F(SetupGuiTest, CameraSettingsPageShowsTheCameraValues) {
    ASSERT_TRUE(loadPage("CameraSettingsPage.qml", "settings", &settings));
    ASSERT_TRUE(spinUntil([&] { return settings.loaded(); }, [this] { step(); }));
    settle();
    const QQuickItem* exposure = named("exposureInput");
    const QQuickItem* gain = named("gainInput");
    ASSERT_NE(exposure, nullptr);
    ASSERT_NE(gain, nullptr);
    EXPECT_NEAR(exposure->property("value").toDouble(), 5000.0, 1e-6);
    EXPECT_NEAR(gain->property("value").toDouble(), 2.0, 1e-6);
}

TEST_F(SetupGuiTest, ApplyButtonSendsExposureAndGain) {
    ASSERT_TRUE(loadPage("CameraSettingsPage.qml", "settings", &settings));
    ASSERT_TRUE(spinUntil([&] { return settings.loaded(); }, [this] { step(); }));
    settle();
    QQuickItem* exposure = named("exposureInput");
    QQuickItem* apply = named("applyButton");
    ASSERT_NE(exposure, nullptr);
    ASSERT_NE(apply, nullptr);

    ASSERT_TRUE(exposure->setProperty("value", 8000.0));
    ASSERT_TRUE(QMetaObject::invokeMethod(apply, "clicked"));
    ASSERT_TRUE(spinUntil([&] { return service.exposureUs(kCam1) > 7999.0; }, [this] { step(); }));
    EXPECT_NEAR(service.exposureUs(kCam1), 8000.0, 1e-6);
}

} // namespace

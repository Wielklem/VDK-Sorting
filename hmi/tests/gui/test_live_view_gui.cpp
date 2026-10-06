#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QUrl>
#include <chrono>
#include <memory>

#include <gtest/gtest.h>

#include "fake_service.hpp"
#include "live/live_view_model.hpp"
#include "live/service_client.hpp"
#include "test_util.hpp"

namespace {

using namespace std::chrono_literals;
using namespace vsort::hmi;
using namespace vsort::hmi::test;

constexpr std::uint16_t kCam1 = 61;
constexpr std::uint16_t kCam2 = 62;

// Renders the real LiveViewPage.qml (from the source tree) offscreen against a fake service.
class LiveViewGuiTest : public ::testing::Test {
protected:
    LiveViewGuiTest()
        : service{{kCam1, kCam2}}
        , client{{.host = QStringLiteral("127.0.0.1"),
                  .commandPort = FakeService::kCommandPort,
                  .eventPort = FakeService::kEventPort}}
        , model{client} {}

    void SetUp() override {
        VSORT_HMI_SKIP_IF_NO_RINGS(service);
        QQmlComponent component{&engine, QUrl::fromLocalFile(QStringLiteral(VSORT_CAMERAS_QML_DIR
                                                                            "/LiveViewPage.qml"))};
        ASSERT_FALSE(component.isError()) << component.errorString().toStdString();
        page.reset(qobject_cast<QQuickItem*>(component.createWithInitialProperties(
            {{QStringLiteral("live"), QVariant::fromValue(&model)}})));
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
    }

    // Wait until both visible grid tiles have a frame in their VideoItem.
    bool waitForPictures() {
        return spinUntil(
            [&] {
                int ready = 0;
                for (const QQuickItem* item : previews()) {
                    if (item->isVisible() && item->property("hasFrame").toBool()) {
                        ++ready;
                    }
                }
                return ready >= 2;
            },
            [this] { step(); });
    }

    // Repeater delegates are not QObject children of the page: walk the visual item tree.
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

    [[nodiscard]] QList<QQuickItem*> previews() const { return all("video"); }

    [[nodiscard]] QQuickItem* named(const char* name) const {
        const auto found = all(name);
        return found.isEmpty() ? nullptr : found.first();
    }

    QImage grab() {
        QCoreApplication::processEvents();
        return window.grabWindow();
    }

    static bool hasContent(const QImage& image, const QRect& area) {
        // True when the area contains more than one distinct gray level (a picture, not a fill).
        int minV = 255;
        int maxV = 0;
        for (int y = area.top(); y < area.bottom(); y += 4) {
            for (int x = area.left(); x < area.right(); x += 4) {
                const int v = qGray(image.pixel(x, y));
                minV = std::min(minV, v);
                maxV = std::max(maxV, v);
            }
        }
        return maxV - minV > 60;
    }

    FakeService service;
    ServiceClient client;
    LiveViewModel model;
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QQuickItem> page;
};

TEST_F(LiveViewGuiTest, GridShowsAllCamerasWithPictures) {
    ASSERT_TRUE(waitForPictures());
    EXPECT_EQ(previews().size(), 4); // grid tile + hidden fullscreen tile per camera

    const QImage shot = grab();
    ASSERT_FALSE(shot.isNull());
    shot.save(QDir::temp().filePath(QStringLiteral("vsort_live_grid.png")));
    // Two columns on a 1280x752 area: left tile x 0..640, right tile x 640..1280.
    EXPECT_TRUE(hasContent(shot, QRect{100, 120, 440, 500}));
    EXPECT_TRUE(hasContent(shot, QRect{740, 120, 440, 500}));
    EXPECT_EQ(named("connectionText")->property("text").toString(), "Service connected");
}

TEST_F(LiveViewGuiTest, FreezeAllButtonFreezesAndUnfreezes) {
    ASSERT_TRUE(waitForPictures());
    QQuickItem* button = named("freezeAllButton");
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->property("text").toString(), "Freeze all");

    ASSERT_TRUE(QMetaObject::invokeMethod(button, "clicked"));
    EXPECT_TRUE(model.allFrozen());
    EXPECT_EQ(button->property("text").toString(), "Unfreeze all");

    const auto before = model.data(model.index(0), LiveViewModel::FrameCounterRole).toULongLong();
    spinFor(300ms, [this] { step(); });
    EXPECT_EQ(model.data(model.index(0), LiveViewModel::FrameCounterRole).toULongLong(), before);
    const QImage frozen = grab();
    frozen.save(QDir::temp().filePath(QStringLiteral("vsort_live_frozen.png")));

    ASSERT_TRUE(QMetaObject::invokeMethod(button, "clicked"));
    EXPECT_FALSE(model.allFrozen());
    EXPECT_TRUE(spinUntil(
        [&] {
            return model.data(model.index(0), LiveViewModel::FrameCounterRole).toULongLong() >
                   before;
        },
        [this] { step(); }));
}

TEST_F(LiveViewGuiTest, ClickingATileShowsItFullscreenAndEscapeReturns) {
    ASSERT_TRUE(waitForPictures());
    ASSERT_TRUE(page->setProperty("focusedId", kCam2));
    QCoreApplication::processEvents();
    EXPECT_TRUE(named("focusLayer")->isVisible());

    const QImage shot = grab();
    shot.save(QDir::temp().filePath(QStringLiteral("vsort_live_fullscreen.png")));
    // Fullscreen: the picture fills the area, so the middle of the window has content.
    EXPECT_TRUE(hasContent(shot, QRect{300, 150, 680, 500}));

    ASSERT_TRUE(QMetaObject::invokeMethod(page.get(), "forceActiveFocus"));
    page->setProperty("focusedId", -1);
    QCoreApplication::processEvents();
    EXPECT_FALSE(named("focusLayer")->isVisible());
}

TEST_F(LiveViewGuiTest, FreezeButtonOnATileFreezesOnlyThatCamera) {
    ASSERT_TRUE(waitForPictures());
    const auto buttons = all("freezeButton");
    ASSERT_GE(buttons.size(), 2);
    // First button belongs to the first grid tile (camera 61).
    ASSERT_TRUE(QMetaObject::invokeMethod(buttons.first(), "clicked"));
    const int frozenRows =
        static_cast<int>(model.data(model.index(0), LiveViewModel::FrozenRole).toBool()) +
        static_cast<int>(model.data(model.index(1), LiveViewModel::FrozenRole).toBool());
    EXPECT_EQ(frozenRows, 1);
    EXPECT_FALSE(model.allFrozen());
}

} // namespace

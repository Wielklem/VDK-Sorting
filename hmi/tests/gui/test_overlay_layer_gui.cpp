#include <QByteArray>
#include <QCoreApplication>
#include <QEvent>
#include <QList>
#include <QPointF>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSizeF>
#include <QString>
#include <QUrl>
#include <memory>

#include <gtest/gtest.h>

namespace {

// Frame 1000x500 in a 500x500 layer: scale 0.5, picture 500x250 centred (y offset 125).
constexpr auto kQml = R"qml(
import QtQuick
import VsortHmiTest

OverlayLayer {
    width: 500
    height: 500
    sourceSize: Qt.size(1000, 500)
    rois: [{x: 100, y: 100, width: 200, height: 100, label: "A"}]
    lanes: [{x1: 0, y1: 0, x2: 1000, y2: 0}]
    detections: [{x: 500, y: 250, width: 100, height: 50, label: "#1"}]
}
)qml";

void collect(QQuickItem* root, const QString& name, QList<QQuickItem*>& out) {
    for (QQuickItem* child : root->childItems()) {
        if (child->objectName() == name) {
            out.push_back(child);
        }
        collect(child, name, out);
    }
}

class OverlayLayerGuiTest : public ::testing::Test {
protected:
    void SetUp() override {
        QQmlComponent component{&engine};
        component.setData(QByteArray(kQml), QUrl{});
        ASSERT_FALSE(component.isError()) << component.errorString().toStdString();
        layer.reset(qobject_cast<QQuickItem*>(component.create()));
        ASSERT_NE(layer, nullptr) << component.errorString().toStdString();
    }

    [[nodiscard]] QList<QQuickItem*> all(const char* name) const {
        QList<QQuickItem*> result;
        collect(layer.get(), QString::fromLatin1(name), result);
        return result;
    }

    static void settle() {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    QQmlEngine engine;
    std::unique_ptr<QQuickItem> layer;
};

TEST_F(OverlayLayerGuiTest, MapsFrameCoordinatesIntoThePictureRect) {
    ASSERT_EQ(all("roi").size(), 1);
    ASSERT_EQ(all("laneLine").size(), 1);
    ASSERT_EQ(all("detection").size(), 1);

    const QQuickItem* roi = all("roi").first();
    const QPointF roiPos = roi->mapToItem(layer.get(), QPointF{0, 0});
    EXPECT_NEAR(roiPos.x(), 50.0, 0.01);
    EXPECT_NEAR(roiPos.y(), 175.0, 0.01);
    EXPECT_NEAR(roi->width(), 100.0, 0.01);
    EXPECT_NEAR(roi->height(), 50.0, 0.01);

    const QQuickItem* det = all("detection").first();
    const QPointF detPos = det->mapToItem(layer.get(), QPointF{0, 0});
    EXPECT_NEAR(detPos.x(), 250.0, 0.01);
    EXPECT_NEAR(detPos.y(), 250.0, 0.01);

    const QQuickItem* lane = all("laneLine").first();
    EXPECT_NEAR(lane->width(), 500.0, 0.01);
    EXPECT_NEAR(lane->rotation(), 0.0, 0.01);
}

TEST_F(OverlayLayerGuiTest, GroupsCanBeSwitchedOffSeparately) {
    ASSERT_TRUE(layer->setProperty("showRois", false));
    settle();
    EXPECT_EQ(all("roi").size(), 0);
    EXPECT_EQ(all("laneLine").size(), 1);
    EXPECT_EQ(all("detection").size(), 1);
}

TEST_F(OverlayLayerGuiTest, DrawsNothingWithoutSourceSize) {
    ASSERT_TRUE(layer->setProperty("sourceSize", QSizeF{0, 0}));
    settle();
    const auto content = all("overlayContent");
    ASSERT_EQ(content.size(), 1);
    EXPECT_FALSE(content.first()->isVisible());
}

} // namespace

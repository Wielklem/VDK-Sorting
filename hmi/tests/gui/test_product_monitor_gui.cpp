
#include <QCoreApplication>
#include <QEvent>
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
#include <string>

#include <gtest/gtest.h>

#include "fake_service.hpp"
#include "live/product_monitor_model.hpp"
#include "live/service_client.hpp"
#include "test_util.hpp"

namespace {

using namespace vsort::hmi;
using namespace vsort::hmi::test;

// One lane: Camera 1 with a "Size" measurement, Camera 2.
const std::string kMachine = R"({"lines": [{"id": 1, "name": "Line 1", "lanes": [
  {"id": 1, "name": "Lane 1", "sensors": [
    {"id": 1, "name": "Camera 1", "kind": "camera", "camera_id": 0, "roi_id": 0,
     "offset_cups": 0, "show_in_monitor": true,
     "measurements": [{"key": "size_mm", "label": "Size", "unit": "mm"}]},
    {"id": 2, "name": "Camera 2", "kind": "camera", "camera_id": 1, "roi_id": 0,
     "offset_cups": 5, "show_in_monitor": true, "measurements": []}]}]}]})";

CupRowData cup(std::int64_t id, CupCellStatus s1, CupCellStatus s2) {
    return {.cupId = id, .cells = {{.sensorId = 1, .status = s1}, {.sensorId = 2, .status = s2}}};
}

// Renders the real G140.10 page (from the source tree) offscreen against a fake service.
class ProductMonitorGuiTest : public ::testing::Test {
protected:
    void SetUp() override { window.resize(1280, 800); }

    void start(const std::string& machine) {
        service.setConfigJson("machine", machine);
        service.setCupSnapshot(
            {LaneCupsData{.laneId = 1,
                          .seq = 1,
                          .depth = 50,
                          .cups = {cup(21, CupCellStatus::Ok, CupCellStatus::Pending),
                                   cup(20, CupCellStatus::Ok, CupCellStatus::NoData)}}});
        client.start();
    }

    bool loadPage() {
        QQmlComponent component{&engine,
                                QUrl::fromLocalFile(QStringLiteral(VSORT_CAMERAS_QML_DIR) +
                                                    QStringLiteral("/ProductMonitorView.qml"))};
        if (component.isError()) {
            ADD_FAILURE() << component.errorString().toStdString();
            return false;
        }
        page.reset(qobject_cast<QQuickItem*>(component.createWithInitialProperties(
            {{QStringLiteral("monitor"), QVariant::fromValue(&model)}})));
        if (!page) {
            ADD_FAILURE() << component.errorString().toStdString();
            return false;
        }
        page->setParentItem(window.contentItem());
        page->setSize(QSizeF{1280, 800});
        window.show();
        return true;
    }

    void step() { service.pump(); }

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

    FakeService service{{}};
    ServiceClient client{{.host = QStringLiteral("127.0.0.1"),
                          .commandPort = FakeService::kCommandPort,
                          .eventPort = FakeService::kEventPort}};
    ProductMonitorModel model{client};
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QQuickItem> page;
};

} // namespace

TEST_F(ProductMonitorGuiTest, ShowsColumnsRowsAndRedNoDataCells) {
    start(kMachine);
    ASSERT_TRUE(loadPage());
    ASSERT_TRUE(spinUntil([&] { return model.rowsModel().rowCount() == 2; }, [this] { step(); }));
    settle();

    EXPECT_EQ(all("columnHeader").size(), 3); // Camera 1, Size [mm], Camera 2
    EXPECT_EQ(all("okCell").size(), 2);
    EXPECT_EQ(all("noDataCell").size(), 1); // cup 20, Camera 2
    const QQuickItem* list = named("cupList");
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->property("count").toInt(), 2);
    EXPECT_FALSE(named("emptyText")->isVisible());
}

TEST_F(ProductMonitorGuiTest, FreezeButtonHoldsTheTable) {
    start(kMachine);
    ASSERT_TRUE(loadPage());
    ASSERT_TRUE(spinUntil([&] { return model.rowsModel().rowCount() == 2; }, [this] { step(); }));
    QQuickItem* button = named("freezeButton");
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->property("text").toString(), QStringLiteral("Freeze"));

    ASSERT_TRUE(QMetaObject::invokeMethod(button, "clicked"));
    settle();
    EXPECT_TRUE(model.frozen());
    EXPECT_EQ(button->property("text").toString(), QStringLiteral("Back to live"));

    service.publishCupUpdate({.laneId = 1,
                              .seq = 2,
                              .depth = 0,
                              .cups = {cup(22, CupCellStatus::Ok, CupCellStatus::Pending)}});
    spinFor(std::chrono::milliseconds{150}, [this] { step(); });
    EXPECT_EQ(named("cupList")->property("count").toInt(), 2);

    ASSERT_TRUE(QMetaObject::invokeMethod(button, "clicked"));
    settle();
    EXPECT_FALSE(model.frozen());
    EXPECT_EQ(named("cupList")->property("count").toInt(), 3);
}

TEST_F(ProductMonitorGuiTest, ShowsWhyThereIsNoTable) {
    start(R"({"lines": []})");
    ASSERT_TRUE(loadPage());
    ASSERT_TRUE(spinUntil(
        [&] {
            return model.connected() && !model.status().isEmpty() &&
                   model.status().startsWith(QStringLiteral("No lanes"));
        },
        [this] { step(); }));
    settle();
    const QQuickItem* text = named("emptyText");
    ASSERT_NE(text, nullptr);
    EXPECT_TRUE(text->isVisible());
    EXPECT_EQ(text->property("text").toString(), QStringLiteral("No lanes in the machine config"));
    EXPECT_FALSE(named("freezeButton")->isEnabled());
}

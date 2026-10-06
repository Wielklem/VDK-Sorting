#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include <QtQml/qqml.h>
#include <gtest/gtest.h>

#include "video/video_item.hpp"

int main(int argc, char* argv[]) {
    // No display and no GPU needed: offscreen platform plus the software scene graph.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    // The page copies import "VsortHmiTest" (see hmi/CMakeLists.txt): VideoItem must live there
    // too.
    qmlRegisterType<vsort::hmi::VideoItem>("VsortHmiTest", 1, 0, "VideoItem");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

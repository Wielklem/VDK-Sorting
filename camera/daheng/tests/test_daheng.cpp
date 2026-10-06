// Runs against the real Galaxy SDK. Needs no camera: it checks scanning and the error paths.
#include <gtest/gtest.h>

#include <vsort/camera/daheng.hpp>

using namespace vsort;
using namespace vsort::camera;

TEST(DahengDiscovery, ScanSucceedsWithoutCameras) {
    const auto discovery = makeDahengDiscovery();
    const auto result = discovery->discover();
    ASSERT_TRUE(result.has_value()) << result.error().what();
}

TEST(DahengCamera, OpenRejectsEmptySerial) {
    const auto camera = makeDahengCamera();
    const auto result = camera->open("");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, Errc::InvalidArgument);
}

TEST(DahengCamera, OpenUnknownSerialFails) {
    const auto camera = makeDahengCamera();
    EXPECT_FALSE(camera->open("NO-SUCH-SERIAL").has_value());
    EXPECT_FALSE(camera->isOpen());
}

TEST(DahengCamera, CallsBeforeOpenReportNotFound) {
    const auto camera = makeDahengCamera();
    EXPECT_EQ(camera->info().error().code, Errc::NotFound);
    EXPECT_EQ(camera->settings().error().code, Errc::NotFound);
    EXPECT_EQ(camera->applySettings(CameraSettings{}).error().code, Errc::NotFound);
    EXPECT_EQ(camera->start().error().code, Errc::NotFound);
    EXPECT_FALSE(camera->isStreaming());
}

TEST(DahengCamera, StopAndCloseAreSafeWhenNotOpen) {
    const auto camera = makeDahengCamera();
    camera->stop();
    camera->close();
    EXPECT_FALSE(camera->isOpen());
}

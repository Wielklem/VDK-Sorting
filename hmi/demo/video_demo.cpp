// Dev-only: shows VideoItem with a generated, moving test pattern.
// Switches between RGB 640x360 and Mono 480x480 every 150 frames (tests resize + aspect fit).
#include <QByteArray>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <QtQml/qqml.h>

#include "video/video_item.hpp"

namespace {

namespace ipc = vsort::ipc;
using vsort::hmi::FramePtr;
using vsort::hmi::VideoItem;

constexpr auto kQml = R"(
import QtQuick
import VideoDemo

Window {
    width: 960
    height: 540
    visible: true
    color: "#202020"
    title: "VideoItem demo"
    VideoItem {
        objectName: "video"
        anchors.fill: parent
    }
}
)";

FramePtr makePattern(std::uint64_t n) {
    const bool mono = (n / 150U) % 2U == 1U;
    const std::uint32_t width = mono ? 480U : 640U;
    const std::uint32_t height = mono ? 480U : 360U;
    const std::uint32_t channels = mono ? 1U : 3U;

    auto frame = std::make_shared<ipc::PreviewFrame>();
    frame->info = ipc::PreviewFrameInfo{.frameId = n,
                                        .timestampNs = 0,
                                        .width = width,
                                        .height = height,
                                        .strideBytes = width * channels,
                                        .pixelFormat = mono ? ipc::PreviewPixelFormat::Mono8
                                                            : ipc::PreviewPixelFormat::Rgb8};
    frame->pixels.resize(std::size_t{width} * height * channels);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const bool border = x == 0U || y == 0U || x == width - 1U || y == height - 1U;
            for (std::uint32_t c = 0; c < channels; ++c) {
                const std::uint64_t value = border ? 255U : (x + y / 2U + n * 3U + c * 80U);
                frame->pixels[(std::size_t{y} * width + x) * channels + c] =
                    std::byte{static_cast<unsigned char>(value & 0xFFU)};
            }
        }
    }
    return frame;
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    qmlRegisterType<VideoItem>("VideoDemo", 1, 0, "VideoItem");

    QQmlApplicationEngine engine;
    engine.loadData(QByteArray(kQml));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    auto* video = engine.rootObjects().first()->findChild<VideoItem*>("video");
    if (video == nullptr) {
        return 1;
    }

    std::uint64_t counter = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, video,
                     [video, &counter] { video->submitFrame(makePattern(counter++)); });
    timer.start(33);
    return QGuiApplication::exec();
}

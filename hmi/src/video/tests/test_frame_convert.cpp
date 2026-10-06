#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "video/frame_convert.hpp"

namespace {

using vsort::hmi::fitRect;
using vsort::hmi::toImage;
namespace ipc = vsort::ipc;

ipc::PreviewFrame makeFrame(std::uint32_t width, std::uint32_t height, std::uint32_t stride,
                            ipc::PreviewPixelFormat format) {
    ipc::PreviewFrame frame;
    frame.info.width = width;
    frame.info.height = height;
    frame.info.strideBytes = stride;
    frame.info.pixelFormat = format;
    frame.pixels.assign(std::size_t{stride} * height, std::byte{0});
    return frame;
}

} // namespace

TEST(FrameConvert, Mono8HonoursStride) {
    auto frame = makeFrame(3, 2, 8, ipc::PreviewPixelFormat::Mono8);
    frame.pixels[0] = std::byte{10};
    frame.pixels[2] = std::byte{30};
    frame.pixels[5] = std::byte{99}; // padding, must not show up
    frame.pixels[8] = std::byte{40};

    const QImage image = toImage(frame);
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.format(), QImage::Format_Grayscale8);
    EXPECT_EQ(image.width(), 3);
    EXPECT_EQ(image.height(), 2);
    EXPECT_EQ(qRed(image.pixel(0, 0)), 10);
    EXPECT_EQ(qRed(image.pixel(2, 0)), 30);
    EXPECT_EQ(qRed(image.pixel(0, 1)), 40);
}

TEST(FrameConvert, Rgb8Pixels) {
    auto frame = makeFrame(2, 1, 6, ipc::PreviewPixelFormat::Rgb8);
    for (std::size_t i = 0; i < 6; ++i) {
        frame.pixels[i] = std::byte{static_cast<unsigned char>(i + 1U)};
    }

    const QImage image = toImage(frame);
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.format(), QImage::Format_RGB888);
    EXPECT_EQ(qRed(image.pixel(1, 0)), 4);
    EXPECT_EQ(qGreen(image.pixel(1, 0)), 5);
    EXPECT_EQ(qBlue(image.pixel(1, 0)), 6);
}

TEST(FrameConvert, RejectsInconsistentFrames) {
    EXPECT_TRUE(toImage(makeFrame(0, 2, 4, ipc::PreviewPixelFormat::Mono8)).isNull());
    EXPECT_TRUE(toImage(makeFrame(4, 0, 4, ipc::PreviewPixelFormat::Mono8)).isNull());
    EXPECT_TRUE(
        toImage(makeFrame(4, 2, 3, ipc::PreviewPixelFormat::Mono8)).isNull()); // stride < row
    EXPECT_TRUE(toImage(makeFrame(100000, 2, 100000, ipc::PreviewPixelFormat::Mono8)).isNull());

    auto shortFrame = makeFrame(4, 2, 4, ipc::PreviewPixelFormat::Mono8);
    shortFrame.pixels.resize(5);
    EXPECT_TRUE(toImage(shortFrame).isNull());
}

TEST(FitRect, LetterboxesAndCentres) {
    const QRectF wide = fitRect(QSizeF{200, 100}, QSizeF{100, 100});
    EXPECT_DOUBLE_EQ(wide.x(), 0.0);
    EXPECT_DOUBLE_EQ(wide.y(), 25.0);
    EXPECT_DOUBLE_EQ(wide.width(), 100.0);
    EXPECT_DOUBLE_EQ(wide.height(), 50.0);

    const QRectF tall = fitRect(QSizeF{200, 100}, QSizeF{400, 100});
    EXPECT_DOUBLE_EQ(tall.x(), 100.0);
    EXPECT_DOUBLE_EQ(tall.y(), 0.0);
    EXPECT_DOUBLE_EQ(tall.width(), 200.0);
    EXPECT_DOUBLE_EQ(tall.height(), 100.0);
}

TEST(FitRect, EmptyInputGivesNullRect) {
    EXPECT_TRUE(fitRect(QSizeF{0, 10}, QSizeF{100, 100}).isNull());
    EXPECT_TRUE(fitRect(QSizeF{10, 10}, QSizeF{0, 100}).isNull());
}

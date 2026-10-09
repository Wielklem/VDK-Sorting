#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "video/frame_convert.hpp"

namespace {

using vsort::hmi::channelMask;
using vsort::hmi::fitRect;
using vsort::hmi::HsvRange;
using vsort::hmi::rangeOverlay;
using vsort::hmi::rgbToHsv;
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

TEST(HsvEditor, RgbToHsvMatchesOpenCv) {
    // Reference values from cv::cvtColor(COLOR_RGB2HSV), 8-bit.
    EXPECT_EQ(rgbToHsv(255, 0, 0), (std::array<int, 3>{0, 255, 255}));
    EXPECT_EQ(rgbToHsv(0, 255, 0), (std::array<int, 3>{60, 255, 255}));
    EXPECT_EQ(rgbToHsv(0, 0, 255), (std::array<int, 3>{120, 255, 255}));
    EXPECT_EQ(rgbToHsv(255, 255, 0), (std::array<int, 3>{30, 255, 255}));
    EXPECT_EQ(rgbToHsv(128, 128, 128), (std::array<int, 3>{0, 0, 128}));
    EXPECT_EQ(rgbToHsv(0, 0, 0), (std::array<int, 3>{0, 0, 0}));
}

TEST(HsvEditor, ChannelMaskAndHistogram) {
    QImage image{2, 1, QImage::Format_RGB888};
    image.setPixel(0, 0, qRgb(255, 0, 0));     // H 0, S 255, V 255
    image.setPixel(1, 0, qRgb(128, 128, 128)); // H 0, S 0, V 128
    const HsvRange range{.lower = {0, 200, 0}, .upper = {10, 255, 200}};

    std::vector<int> histogram;
    const auto s = channelMask(image, 1, range, histogram);
    ASSERT_EQ(s.format(), QImage::Format_RGB888);
    EXPECT_EQ(s.pixel(0, 0), qRgb(127, 255, 127)); // S 255 (grey 255), in range: 50 % green
    EXPECT_EQ(s.pixel(1, 0), qRgb(0, 0, 0));       // S 0 (grey 0), out of range
    ASSERT_EQ(histogram.size(), 256U);
    EXPECT_EQ(histogram[255], 1);
    EXPECT_EQ(histogram[0], 1);

    const auto v = channelMask(image, 2, range, histogram);
    EXPECT_EQ(v.pixel(0, 0), qRgb(255, 255, 255)); // V 255 > 200: out of range, plain grey
    EXPECT_EQ(v.pixel(1, 0), qRgb(64, 191, 64));   // V 128, in range: grey 128 + 50 % green

    QImage hue{1, 1, QImage::Format_RGB888};
    hue.setPixel(0, 0, qRgb(0, 0, 255)); // H 120 -> grey 120 * 255 / 179 = 170
    const auto h = channelMask(hue, 0, range, histogram);
    EXPECT_EQ(h.pixel(0, 0), qRgb(170, 170, 170)); // H 120 outside 0..10
    ASSERT_EQ(histogram.size(), 180U);             // H 0..179
    EXPECT_EQ(histogram[120], 1);
}

TEST(HsvEditor, RangeOverlayMarksPixelsInAllThreeRangesGreen) {
    QImage image{2, 1, QImage::Format_RGB888};
    image.setPixel(0, 0, qRgb(200, 0, 0));     // H 0, S 255, V 200: in range
    image.setPixel(1, 0, qRgb(128, 128, 128)); // S 0: out
    std::size_t inRange = 0;
    const auto out =
        rangeOverlay(image, HsvRange{.lower = {0, 200, 0}, .upper = {10, 255, 255}}, inRange);
    EXPECT_EQ(inRange, 1U);
    ASSERT_EQ(out.format(), QImage::Format_RGB888);
    EXPECT_EQ(out.pixel(0, 0), qRgb(100, 127, 0));
    EXPECT_EQ(out.pixel(1, 0), qRgb(128, 128, 128));
}

#include <gtest/gtest.h>

#include "ipc/downscale.hpp"
#include "test_ipc_util.hpp"

using namespace vsort;
using namespace vsort::service;
using vsort::camera::PixelFormat;
using vsort::testutil::makeFrame;

TEST(PreviewSize, KeepsAspectAndNeverUpscales) {
    EXPECT_EQ(previewSize(1280, 1024, 640).width, 640U);
    EXPECT_EQ(previewSize(1280, 1024, 640).height, 512U);
    EXPECT_EQ(previewSize(320, 240, 640).width, 320U);
    EXPECT_EQ(previewSize(320, 240, 640).height, 240U);
    EXPECT_EQ(previewSize(1000, 333, 100).height, 33U);
    EXPECT_EQ(previewSize(5000, 1, 100).height, 1U); // never 0
}

TEST(PreviewSize, ZeroInputGivesZero) {
    EXPECT_EQ(previewSize(0, 10, 100).width, 0U);
    EXPECT_EQ(previewSize(10, 0, 100).height, 0U);
    EXPECT_EQ(previewSize(10, 10, 0).width, 0U);
}

TEST(Downscale, MonoStaysMono) {
    const auto f = makeFrame(0, 1, 128, 64, 90);
    const auto img = downscaleForPreview(f->frame, 64);
    ASSERT_TRUE(img.has_value()) << img.error().what();
    EXPECT_EQ(img->width, 64U);
    EXPECT_EQ(img->height, 32U);
    EXPECT_EQ(img->format, ipc::PreviewPixelFormat::Mono8);
    ASSERT_EQ(img->pixels.size(), 64U * 32U);
    EXPECT_EQ(img->pixels.front(), std::byte{90});
    EXPECT_EQ(img->pixels.back(), std::byte{90});
}

TEST(Downscale, SmallFrameIsCopiedUnscaled) {
    const auto f = makeFrame(0, 1, 32, 16, 7);
    const auto img = downscaleForPreview(f->frame, 640);
    ASSERT_TRUE(img.has_value());
    EXPECT_EQ(img->width, 32U);
    EXPECT_EQ(img->height, 16U);
}

TEST(Downscale, RowPaddingIsHonoured) {
    const auto f = makeFrame(0, 1, 100, 50, 200, PixelFormat::Mono8, /*padding=*/28);
    const auto img = downscaleForPreview(f->frame, 50);
    ASSERT_TRUE(img.has_value()) << img.error().what();
    for (const std::byte b : img->pixels) {
        ASSERT_EQ(b, std::byte{200}); // padding bytes (also 200 here) must not shift rows
    }
    EXPECT_EQ(img->pixels.size(), 50U * 25U);
}

TEST(Downscale, RgbKeepsChannelOrderAndBgrIsSwapped) {
    auto rgb = makeFrame(0, 1, 16, 16, 0, PixelFormat::Rgb8);
    for (std::size_t i = 0; i < rgb->bytes.size(); i += 3) {
        rgb->bytes[i] = std::byte{255}; // R
    }
    const auto a = downscaleForPreview(rgb->frame, 8);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->format, ipc::PreviewPixelFormat::Rgb8);
    EXPECT_EQ(a->pixels[0], std::byte{255});
    EXPECT_EQ(a->pixels[1], std::byte{0});
    EXPECT_EQ(a->pixels[2], std::byte{0});

    auto bgr = makeFrame(0, 1, 16, 16, 0, PixelFormat::Bgr8);
    for (std::size_t i = 0; i < bgr->bytes.size(); i += 3) {
        bgr->bytes[i] = std::byte{255}; // B
    }
    const auto b = downscaleForPreview(bgr->frame, 8);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->pixels[0], std::byte{0});
    EXPECT_EQ(b->pixels[2], std::byte{255}); // blue ends up last in RGB
}

TEST(Downscale, BayerBecomesRgb) {
    const auto f = makeFrame(0, 1, 64, 32, 128, PixelFormat::BayerRG8);
    const auto img = downscaleForPreview(f->frame, 32);
    ASSERT_TRUE(img.has_value()) << img.error().what();
    EXPECT_EQ(img->format, ipc::PreviewPixelFormat::Rgb8);
    EXPECT_EQ(img->pixels.size(), 32U * 16U * 3U);
}

TEST(Downscale, RejectsBadFrames) {
    auto f = makeFrame(0, 1, 64, 32, 1);
    EXPECT_EQ(downscaleForPreview(f->frame, 0).error().code, Errc::InvalidArgument);

    auto shortStride = makeFrame(0, 1, 64, 32, 1);
    shortStride->frame.meta.strideBytes = 10;
    EXPECT_EQ(downscaleForPreview(shortStride->frame, 32).error().code, Errc::InvalidArgument);

    auto shortBuffer = makeFrame(0, 1, 64, 32, 1);
    shortBuffer->frame.data = shortBuffer->frame.data.first(100);
    EXPECT_EQ(downscaleForPreview(shortBuffer->frame, 32).error().code, Errc::InvalidArgument);

    auto empty = makeFrame(0, 1, 64, 32, 1);
    empty->frame.meta.width = 0;
    EXPECT_EQ(downscaleForPreview(empty->frame, 32).error().code, Errc::InvalidArgument);
}

#include <gtest/gtest.h>

#include "test_ipc_util.hpp"
#include "tracking/lane_crop.hpp"

using namespace vsort;
using namespace vsort::service;
using vsort::camera::PixelFormat;
using vsort::testutil::makeFrame;

namespace {

const nlohmann::json kRois = nlohmann::json::parse(R"({"cameras": [{"camera_id": 2, "rois": [
  {"id": 1, "name": "Lane 1", "x": 0.1, "y": 0.2, "width": 0.3, "height": 0.4}]}]})");

} // namespace

TEST(LaneCrop, FindsRoiAndWholeImage) {
    const auto roi = findRoi(kRois, 2, 1);
    ASSERT_TRUE(roi.has_value());
    EXPECT_DOUBLE_EQ(roi->x, 0.1);
    EXPECT_DOUBLE_EQ(roi->height, 0.4);
    const auto whole = findRoi(kRois, 7, 0);
    ASSERT_TRUE(whole.has_value());
    EXPECT_DOUBLE_EQ(whole->width, 1.0);
    EXPECT_FALSE(findRoi(kRois, 2, 9).has_value());
    EXPECT_FALSE(findRoi(kRois, 1, 1).has_value()); // ROI 1 belongs to camera 2
    EXPECT_FALSE(findRoi(nlohmann::json::object(), 2, 1).has_value());
}

TEST(LaneCrop, FractionsToPixels) {
    const auto r = toPixels({.x = 0.1, .y = 0.2, .width = 0.3, .height = 0.4}, 100, 50, 1);
    EXPECT_EQ(r.x, 10U);
    EXPECT_EQ(r.y, 10U);
    EXPECT_EQ(r.width, 30U);
    EXPECT_EQ(r.height, 20U);
    const auto whole = toPixels({}, 1280, 1024, 1);
    EXPECT_EQ(whole.width, 1280U);
    EXPECT_EQ(whole.height, 1024U);
}

TEST(LaneCrop, ClampsToTheImageAndKeepsOnePixel) {
    const auto r = toPixels({.x = 0.9, .y = 0.0, .width = 0.5, .height = 0.0001}, 100, 100, 1);
    EXPECT_EQ(r.x, 90U);
    EXPECT_EQ(r.width, 10U);
    EXPECT_EQ(r.height, 1U);
    EXPECT_EQ(toPixels({}, 0, 100, 1).width, 0U);
}

TEST(LaneCrop, BayerCropsStayOnEvenPixels) {
    const auto r = toPixels({.x = 0.013, .y = 0.027, .width = 0.311, .height = 0.5}, 101, 99, 2);
    EXPECT_EQ(r.x % 2, 0U);
    EXPECT_EQ(r.y % 2, 0U);
    EXPECT_EQ(r.width % 2, 0U);
    EXPECT_EQ(r.height % 2, 0U);
    EXPECT_LE(r.x + r.width, 101U);
    EXPECT_LE(r.y + r.height, 99U);
    const auto edge = toPixels({.x = 0.99, .y = 0.99, .width = 0.01, .height = 0.01}, 101, 99, 2);
    EXPECT_EQ(edge.width, 2U);
    EXPECT_EQ(edge.height, 2U);
    EXPECT_LE(edge.x + edge.width, 101U);
    EXPECT_EQ(cropAlignment(PixelFormat::BayerRG8), 2U);
    EXPECT_EQ(cropAlignment(PixelFormat::Mono8), 1U);
}

TEST(LaneCrop, CropViewPointsIntoTheFrame) {
    auto f = makeFrame(2, 1, 64, 32, 0, PixelFormat::Rgb8, /*padding=*/8);
    const auto stride = f->frame.meta.strideBytes;    // 64 * 3 + 8
    f->bytes[(5 * stride) + (4 * 3)] = std::byte{77}; // pixel (4, 5), R
    const auto view = cropFrame(f->frame, {.x = 4, .y = 5, .width = 10, .height = 3});
    ASSERT_TRUE(view.has_value()) << view.error().what();
    EXPECT_EQ(view->data.front(), std::byte{77});
    EXPECT_EQ(view->strideBytes, stride);
    EXPECT_EQ(view->data.size(), (2U * stride) + (10U * 3U));
    EXPECT_EQ(view->owner, f->frame.owner);
    EXPECT_EQ(view->data.data(), f->bytes.data() + (5 * stride) + (4 * 3));
}

TEST(LaneCrop, CropViewRejectsBadCrops) {
    const auto f = makeFrame(0, 1, 64, 32, 0);
    EXPECT_FALSE(cropFrame(f->frame, {.x = 60, .y = 0, .width = 10, .height = 1}).has_value());
    EXPECT_FALSE(cropFrame(f->frame, {.x = 0, .y = 0, .width = 0, .height = 1}).has_value());
    auto shortFrame = makeFrame(0, 1, 64, 32, 0);
    shortFrame->frame.data = std::span<const std::byte>{shortFrame->bytes}.first(100);
    EXPECT_FALSE(cropFrame(shortFrame->frame, {.x = 0, .y = 0, .width = 64, .height = 32}));
}

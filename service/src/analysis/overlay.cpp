#include "analysis/overlay.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>

namespace vsort::service {

namespace {

const cv::Scalar kTint{0, 255, 255};        // yellow
const cv::Scalar kRoiColor{255, 255, 255};  // white
const cv::Scalar kNeighbour{128, 128, 128}; // grey
const std::array<cv::Scalar, 4> kBlobColors{
    {{0, 255, 0}, {255, 128, 0}, {255, 0, 255}, {0, 128, 255}}};

std::vector<cv::Point> boxPoints(const Blob& blob) {
    std::vector<cv::Point> points;
    points.reserve(blob.box.size());
    for (const auto& p : blob.box) {
        points.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    return points;
}

} // namespace

cv::Mat drawOverlay(const AnalysisContext& ctx) {
    if (ctx.bgr.empty()) {
        return {};
    }
    cv::Mat img = ctx.bgr.clone();
    if (!ctx.mask.empty() && ctx.mask.size() == img.size()) {
        cv::Mat tint{img.size(), img.type(), cv::Scalar{0, 0, 0}};
        tint.setTo(kTint, ctx.mask);
        cv::addWeighted(img, 1.0, tint, 0.35, 0.0, img);
    }
    cv::rectangle(img, ctx.roi, kRoiColor, 2);

    for (std::size_t i = 0; i < ctx.blobs.size(); ++i) {
        const auto& blob = ctx.blobs[i];
        const auto& color = kBlobColors.at(i % kBlobColors.size());
        cv::polylines(img, blob.contour, true, color, 2, cv::LINE_AA);
        cv::polylines(img, boxPoints(blob), true, color, 1, cv::LINE_AA);
        const std::string label = std::format(
            "#{} L={:.1f} W={:.1f} mm", i, blob.lengthPx * ctx.mmPerPx, blob.widthPx * ctx.mmPerPx);
        cv::putText(img, label, cv::Point{cvRound(blob.centroid.x) - 40, cvRound(blob.centroid.y)},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1, cv::LINE_AA);
    }
    for (const auto& blob : ctx.neighbours) {
        cv::polylines(img, blob.contour, true, kNeighbour, 1, cv::LINE_AA);
        cv::putText(img, "neighbour",
                    cv::Point{cvRound(blob.centroid.x) - 30, cvRound(blob.centroid.y)},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, kNeighbour, 1, cv::LINE_AA);
    }
    std::string summary = "count=" + std::to_string(ctx.blobs.size()) +
                          " neighbours=" + std::to_string(ctx.neighbours.size());
    for (const auto& v : ctx.values) {
        if (v.key == kKeyMaskPct) {
            summary += std::format(" mask={:.1f}%", v.value);
        }
    }
    cv::putText(img, summary, cv::Point{8, 20}, cv::FONT_HERSHEY_SIMPLEX, 0.6, kRoiColor, 1,
                cv::LINE_AA);
    return img;
}

} // namespace vsort::service

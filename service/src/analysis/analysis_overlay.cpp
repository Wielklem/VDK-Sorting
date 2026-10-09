#include "analysis/analysis_overlay.hpp"

#include <opencv2/imgproc.hpp>

namespace vsort::service {

namespace {
constexpr double kContourEpsilonPx = 1.0; // approxPolyDP tolerance: small messages, same shape

OverlayObject toObject(const Blob& blob, const cv::Point& origin, double mmPerPx, bool counted) {
    std::vector<cv::Point> simple;
    cv::approxPolyDP(blob.contour, simple, kContourEpsilonPx, true);
    OverlayObject out;
    out.counted = counted;
    out.lengthMm = blob.lengthPx * mmPerPx;
    out.widthMm = blob.widthPx * mmPerPx;
    out.contour.reserve(simple.size() * 2);
    for (const auto& p : simple) {
        out.contour.push_back(p.x + origin.x);
        out.contour.push_back(p.y + origin.y);
    }
    return out;
}
} // namespace

AnalysisOverlay makeOverlay(const AnalysisContext& ctx, const AnalysisRegion& region,
                            const camera::Roi& lane, const camera::FrameMetadata& meta,
                            std::uint16_t sensorId) {
    AnalysisOverlay overlay{.cameraId = meta.cameraIndex,
                            .sensorId = sensorId,
                            .frameId = meta.frameId.value(),
                            .frameWidth = meta.width,
                            .frameHeight = meta.height,
                            .lane = lane,
                            .objects = {}};
    const cv::Point origin{static_cast<int>(region.crop.x), static_cast<int>(region.crop.y)};
    overlay.objects.reserve(ctx.blobs.size() + ctx.neighbours.size());
    for (const auto& blob : ctx.blobs) {
        overlay.objects.push_back(toObject(blob, origin, ctx.mmPerPx, true));
    }
    for (const auto& blob : ctx.neighbours) {
        overlay.objects.push_back(toObject(blob, origin, ctx.mmPerPx, false));
    }
    return overlay;
}

} // namespace vsort::service

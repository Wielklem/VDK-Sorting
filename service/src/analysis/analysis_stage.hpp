#pragma once

#include <array>
#include <string_view>
#include <vector>

#include <opencv2/core.hpp>

#include <vsort/camera/camera.hpp>
#include <vsort/common/error.hpp>

#include "analysis/measurement.hpp"

namespace vsort::service {

// One object candidate (connected component of the mask), in context (crop) coordinates.
struct Blob {
    std::vector<cv::Point> contour; // outer boundary; enclosed holes are filled
    std::vector<cv::Point> hull;
    double areaPx{0.0};     // pixels inside the contour
    double hullAreaPx{0.0}; // area of the convex hull
    double diameterPx{0.0}; // max(bounding-box diagonal, fitted ellipse major axis)
    double solidity{0.0};   // areaPx / hullAreaPx; a round object is close to 1
    cv::Point2d centroid;   // of the hull
    // Size stage (P60.60): extent along and across the principal axis (image moments).
    double lengthPx{0.0};
    double widthPx{0.0};
    std::array<cv::Point2f, 4> box{}; // corners of the oriented rectangle
};

// Working data of one pipeline run: one sensor, one frame.
struct AnalysisContext {
    // Input, set by the runner. `raw` views the camera pixels of the lane ROI plus the detection
    // buffer around it; it is not owned and only valid during the run.
    cv::Mat raw;
    camera::PixelFormat pixelFormat{camera::PixelFormat::Mono8};
    cv::Rect roi; // the lane ROI inside `raw`

    // Filled by the stages, in pipeline order.
    cv::Mat bgr;                  // preprocess: 8-bit BGR
    cv::Mat mask;                 // segmentation: 255 = object colour, size of `raw`
    std::vector<Blob> blobs;      // segmentation: valid and centred in `roi`, largest first
    std::vector<Blob> neighbours; // segmentation: valid but centred in the buffer (other cup)
    double mmPerPx{0.0};          // size
    std::vector<MeasurementValue> values;
};

// M60 (P60.10): one step of the analysis pipeline. Stages are built per sensor with their
// parameters, are const while running and are only used by one thread at a time.
class IAnalysisStage {
public:
    IAnalysisStage() = default;
    virtual ~IAnalysisStage() = default;
    IAnalysisStage(const IAnalysisStage&) = delete;
    IAnalysisStage& operator=(const IAnalysisStage&) = delete;
    IAnalysisStage(IAnalysisStage&&) = delete;
    IAnalysisStage& operator=(IAnalysisStage&&) = delete;

    // Static string, used in timings and logs.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // An error stops the pipeline for this frame (no measurement for the cup).
    [[nodiscard]] virtual Result<> process(AnalysisContext& context) const = 0;
};

} // namespace vsort::service

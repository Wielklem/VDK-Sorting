#pragma once

#include <cstdint>
#include <vector>

#include <vsort/camera/camera.hpp>

#include "analysis/analysis_stage.hpp"
#include "analysis/pipeline.hpp"

namespace vsort::service {

// One object the segmentation found, in frame pixels.
struct OverlayObject {
    std::vector<std::int32_t> contour; // x0, y0, x1, y1, ... (simplified, about 1 px)
    bool counted{false};               // centred in the lane ROI; false: a neighbour cup
    double lengthMm{0.0};
    double widthMm{0.0};
};

// MSG-60-02 (P60.90): what the pipeline found in one frame of one sensor, for the G60.30 detector
// view in the HMI (AnalysisOverlayEvent). Published on the bus by the analysis module per frame.
struct AnalysisOverlay {
    std::uint16_t cameraId{0};
    std::uint16_t sensorId{0};
    std::uint64_t frameId{0};
    std::uint32_t frameWidth{0};
    std::uint32_t frameHeight{0};
    camera::Roi lane; // lane ROI in frame pixels
    std::vector<OverlayObject> objects;
};

// The objects of a finished pipeline run (after the size stage), moved from crop to frame pixels.
[[nodiscard]] AnalysisOverlay makeOverlay(const AnalysisContext& ctx, const AnalysisRegion& region,
                                          const camera::Roi& lane,
                                          const camera::FrameMetadata& meta,
                                          std::uint16_t sensorId);

} // namespace vsort::service

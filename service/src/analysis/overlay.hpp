#pragma once

#include <opencv2/core.hpp>

#include "analysis/analysis_stage.hpp"

namespace vsort::service {

// Debug image of one pipeline run (BGR): mask tint, lane ROI, counted objects with their
// oriented box and size, neighbour objects in grey, summary line. Empty if nothing was decoded.
// Will feed G60.30 debug overlays; for now only written to disk (debug_every_n).
[[nodiscard]] cv::Mat drawOverlay(const AnalysisContext& context);

} // namespace vsort::service

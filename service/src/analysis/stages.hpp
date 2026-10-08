#pragma once

#include <string_view>

#include "analysis/analysis_config.hpp"
#include "analysis/analysis_stage.hpp"

namespace vsort::service {

// V1 stages, ported from the eqraftvision cups mode (one object per cup).

// P60.30 (colour conversion only): camera pixels -> 8-bit BGR. Bayer RG is demosaiced.
class PreprocessStage final : public IAnalysisStage {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "preprocess"; }
    [[nodiscard]] Result<> process(AnalysisContext& context) const override;
};

// P60.40: HSV range -> mask -> erode/dilate -> connected components with enclosed holes filled
// -> filter (min area, diameter, hull area, aspect ratio, solidity) -> an object counts for this
// cup when the centre of its hull lies inside the lane ROI. Adds "count", "present" and
// "mask_pct".
class ColorSegmentationStage final : public IAnalysisStage {
public:
    explicit ColorSegmentationStage(const AnalysisParams& params)
        : params_{params} {}
    [[nodiscard]] std::string_view name() const noexcept override { return "segmentation"; }
    [[nodiscard]] Result<> process(AnalysisContext& context) const override;

private:
    AnalysisParams params_;
};

// P60.60: orientation from image moments (area-weighted, robust against a jagged edge), length
// and width from projecting the hull on that axis. Adds "length_mm", "width_mm" and "area_mm2"
// of the largest object when there is one.
class SizeStage final : public IAnalysisStage {
public:
    explicit SizeStage(const AnalysisParams& params)
        : params_{params} {}
    [[nodiscard]] std::string_view name() const noexcept override { return "size"; }
    [[nodiscard]] Result<> process(AnalysisContext& context) const override;

private:
    AnalysisParams params_;
};

} // namespace vsort::service

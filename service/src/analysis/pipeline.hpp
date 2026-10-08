#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/common/error.hpp>

#include "analysis/analysis_config.hpp"
#include "analysis/analysis_stage.hpp"

namespace vsort::service {

struct StageTiming {
    std::string_view stage; // IAnalysisStage::name()
    double ms{0.0};
};

// M60 (P60.10): runs its stages in order on one context. Built per sensor; one thread at a time.
class AnalysisPipeline {
public:
    AnalysisPipeline() = default;
    ~AnalysisPipeline() = default;
    AnalysisPipeline(const AnalysisPipeline&) = delete;
    AnalysisPipeline& operator=(const AnalysisPipeline&) = delete;
    AnalysisPipeline(AnalysisPipeline&&) noexcept = default;
    AnalysisPipeline& operator=(AnalysisPipeline&&) noexcept = default;

    void add(std::unique_ptr<IAnalysisStage> stage);

    // Stops at the first failing stage. `timings` gets one entry per stage that ran.
    [[nodiscard]] Result<> run(AnalysisContext& context, std::vector<StageTiming>& timings) const;

    [[nodiscard]] std::vector<std::string_view> stageNames() const;

private:
    std::vector<std::unique_ptr<IAnalysisStage>> stages_;
};

// V1 cup pipeline (one object per cup): preprocess -> colour segmentation -> size. P60.20 moves
// the stage list into the recipe.
[[nodiscard]] AnalysisPipeline makeCupPipeline(const AnalysisParams& params);

// Lane ROI plus the detection buffer, in frame pixels, and the lane ROI inside it.
struct AnalysisRegion {
    camera::Roi crop; // what the pipeline sees
    cv::Rect roi;     // the lane ROI, relative to `crop`
};

// Grows the lane ROI by `buffer`, clamped to the image; `alignment` (2 for Bayer) keeps the crop
// origin and size on the colour pattern.
[[nodiscard]] AnalysisRegion analysisRegion(const camera::Roi& laneRoi, const EdgeMargins& buffer,
                                            std::uint32_t imageWidth, std::uint32_t imageHeight,
                                            std::uint32_t alignment) noexcept;

// Points context.raw at the crop of `frame` (zero copy) and sets the pixel format and ROI.
// The frame must stay alive while the context is used. InvalidArgument: crop outside the frame.
[[nodiscard]] Result<> prepareContext(const camera::Frame& frame, const AnalysisRegion& region,
                                      AnalysisContext& context);

} // namespace vsort::service

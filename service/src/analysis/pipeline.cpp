#include "analysis/pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include "analysis/stages.hpp"
#include "tracking/lane_crop.hpp"

namespace vsort::service {

void AnalysisPipeline::add(std::unique_ptr<IAnalysisStage> stage) {
    stages_.push_back(std::move(stage));
}

Result<> AnalysisPipeline::run(AnalysisContext& context, std::vector<StageTiming>& timings) const {
    timings.clear();
    for (const auto& stage : stages_) {
        const auto start = std::chrono::steady_clock::now();
        auto result = stage->process(context);
        const std::chrono::duration<double, std::milli> took =
            std::chrono::steady_clock::now() - start;
        timings.push_back({.stage = stage->name(), .ms = took.count()});
        if (!result) {
            return result;
        }
    }
    return {};
}

std::vector<std::string_view> AnalysisPipeline::stageNames() const {
    std::vector<std::string_view> names;
    names.reserve(stages_.size());
    for (const auto& stage : stages_) {
        names.push_back(stage->name());
    }
    return names;
}

AnalysisPipeline makeCupPipeline(const AnalysisParams& params) {
    AnalysisPipeline pipeline;
    pipeline.add(std::make_unique<PreprocessStage>());
    pipeline.add(std::make_unique<ColorSegmentationStage>(params));
    pipeline.add(std::make_unique<SizeStage>(params));
    return pipeline;
}

namespace {

// One axis: [begin, end) grown by the margins, clamped to [0, size), aligned.
void growAxis(std::uint32_t begin, std::uint32_t length, std::uint32_t before, std::uint32_t after,
              std::uint32_t size, std::uint32_t alignment, std::uint32_t& outBegin,
              std::uint32_t& outLength) noexcept {
    const std::uint64_t end = std::min<std::uint64_t>(std::uint64_t{begin} + length, size);
    std::uint64_t b = begin > before ? begin - before : 0U;
    std::uint64_t e = std::min<std::uint64_t>(end + after, size);
    b -= b % alignment;
    e -= (e - b) % alignment;
    outBegin = static_cast<std::uint32_t>(b);
    outLength = static_cast<std::uint32_t>(e - b);
}

} // namespace

AnalysisRegion analysisRegion(const camera::Roi& laneRoi, const EdgeMargins& buffer,
                              std::uint32_t imageWidth, std::uint32_t imageHeight,
                              std::uint32_t alignment) noexcept {
    const std::uint32_t a = std::max(alignment, 1U);
    AnalysisRegion region;
    growAxis(laneRoi.x, laneRoi.width, buffer.left, buffer.right, imageWidth, a, region.crop.x,
             region.crop.width);
    growAxis(laneRoi.y, laneRoi.height, buffer.top, buffer.bottom, imageHeight, a, region.crop.y,
             region.crop.height);
    const cv::Rect lane{static_cast<int>(laneRoi.x) - static_cast<int>(region.crop.x),
                        static_cast<int>(laneRoi.y) - static_cast<int>(region.crop.y),
                        static_cast<int>(laneRoi.width), static_cast<int>(laneRoi.height)};
    region.roi = lane & cv::Rect{0, 0, static_cast<int>(region.crop.width),
                                 static_cast<int>(region.crop.height)};
    return region;
}

Result<> prepareContext(const camera::Frame& frame, const AnalysisRegion& region,
                        AnalysisContext& context) {
    auto lane = cropFrame(frame, region.crop);
    if (!lane) {
        return std::unexpected{std::move(lane.error())};
    }
    const int type = bytesPerPixel(lane->pixelFormat) == 3U ? CV_8UC3 : CV_8UC1;
    // cv::Mat has no const view: the pipeline only reads `raw` (see AnalysisContext).
    auto* pixels = const_cast<std::byte*>(lane->data.data()); // NOLINT(*-const-cast)
    context.raw = cv::Mat{static_cast<int>(lane->height), static_cast<int>(lane->width), type,
                          pixels, static_cast<std::size_t>(lane->strideBytes)};
    context.pixelFormat = lane->pixelFormat;
    context.roi = region.roi;
    return {};
}

} // namespace vsort::service

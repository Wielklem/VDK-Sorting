#pragma once

#include <cstdint>
#include <vector>

#include "analysis/analysis_overlay.hpp"

namespace vsort::service {

// Complete event envelope (AnalysisOverlayEvent, MSG-60-02, P60.90) for the "ana" topic.
[[nodiscard]] std::vector<std::uint8_t> makeAnalysisOverlayEnvelope(const AnalysisOverlay& overlay);

} // namespace vsort::service

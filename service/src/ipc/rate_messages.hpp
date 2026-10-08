#pragma once

#include <cstdint>
#include <vector>

#include "analysis/camera_rates.hpp"

namespace vsort::service {

// Complete event envelope (CameraRatesEvent, MSG-30-03, P30.86) for the "cam" topic.
[[nodiscard]] std::vector<std::uint8_t> makeCameraRatesEnvelope(const CameraRates& rates);

} // namespace vsort::service

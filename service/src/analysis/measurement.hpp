#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <vsort/common/types.hpp>

namespace vsort::service {

// Measurement keys of the V1 cup pipeline (P60.10). The machine config's measurement catalog
// (per sensor) picks which of them the Product Monitor shows.
inline constexpr std::string_view kKeyCount = "count";      // objects centred in the lane ROI
inline constexpr std::string_view kKeyMaskPct = "mask_pct"; // % of the ROI with object colour
inline constexpr std::string_view kKeyLength = "length_mm"; // largest object, along its axis
inline constexpr std::string_view kKeyWidth = "width_mm";   // largest object, across its axis
inline constexpr std::string_view kKeyArea = "area_mm2";    // largest object, holes filled

struct MeasurementValue {
    std::string key;
    double value{0.0};

    bool operator==(const MeasurementValue&) const = default;
};

// MSG-60-01 (P60.10): the measurements of one sensor for one cup, from the photo of that cup.
// A later Measurement for the same lane, cup and sensor replaces the earlier one (it follows
// the ObjectRecord corrections of P40.40). Values a stage could not measure are left out.
struct Measurement {
    std::uint16_t laneId{0};
    std::int64_t cupId{0};
    std::uint16_t sensorId{0};
    std::uint16_t cameraId{0};
    FrameId frameId;
    std::vector<MeasurementValue> values;
};

} // namespace vsort::service

#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

#include <vsort/common/config_store.hpp>
#include <vsort/common/error.hpp>

namespace vsort {

// Config module name (a-z, 0-9, '_').
inline constexpr std::string_view kRoiModule = "rois";

// ROIs per logical camera (P30.80). Coordinates are fractions (0..1) of the camera image, so they
// do not depend on the preview size. Config JSON:
//   {"cameras": [{"camera_id": 0, "rois": [
//      {"id": 1, "name": "ROI 1", "x": 0.1, "y": 0.2, "width": 0.3, "height": 0.4}]}]}
[[nodiscard]] nlohmann::json roiSchema();
[[nodiscard]] nlohmann::json roiDefaults(); // no cameras

// Registers the module with the defaults above.
[[nodiscard]] Result<> registerRoiConfig(IConfigStore& store);

} // namespace vsort

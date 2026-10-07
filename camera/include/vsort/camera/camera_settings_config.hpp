#pragma once

#include <cstdint>
#include <map>
#include <string_view>

#include <nlohmann/json.hpp>

#include <vsort/camera/camera.hpp>
#include <vsort/common/config_store.hpp>
#include <vsort/common/error.hpp>

namespace vsort::camera {

// Config module name (a-z, 0-9, '_').
inline constexpr std::string_view kCameraSettingsModule = "camera_settings";

// Saved settings per logical camera ID (P30.85). Config JSON:
//   {"cameras": [{"id": 0, "exposure_us": 10000.0, "gain_db": 0.0, "trigger": "hardware",
//                 "edge": "rising", "roi": {"x": 0, "y": 0, "width": 0, "height": 0}}]}
// trigger: free_run | software | hardware. ROI width/height 0 = full sensor.
[[nodiscard]] nlohmann::json cameraSettingsSchema();
[[nodiscard]] nlohmann::json cameraSettingsDefaults(); // no cameras

[[nodiscard]] nlohmann::json cameraSettingsToJson(std::uint16_t id, const CameraSettings& settings);

// ValidationFailed: schema violation or duplicate ID.
[[nodiscard]] Result<std::map<std::uint16_t, CameraSettings>>
settingsFromJson(const nlohmann::json& config);

[[nodiscard]] Result<> registerCameraSettings(IConfigStore& store);
[[nodiscard]] Result<std::map<std::uint16_t, CameraSettings>>
loadCameraSettings(const IConfigStore& store);

// Read-modify-write of one camera's entry; the other cameras stay as they are.
// Not atomic: callers serialize.
[[nodiscard]] Result<> saveCameraSettings(IConfigStore& store, std::uint16_t id,
                                          const CameraSettings& settings, std::string_view author);

} // namespace vsort::camera

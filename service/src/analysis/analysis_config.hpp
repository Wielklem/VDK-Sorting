#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string_view>

#include <nlohmann/json.hpp>

#include <vsort/common/config_store.hpp>
#include <vsort/common/error.hpp>

namespace vsort::service {

// Config module "analysis" (P60.10). Parameters of the V1 cup pipeline, ported from the
// eqraftvision cups mode. The defaults are the eqraftvision values of the current test set-up
// (potatoes, RGB cameras, white balance R 1.2 / G 0.9 / B 2.0 set in the camera); tune them for
// eggs later. Until the recipes (P60.20) there is one set of defaults plus optional overrides
// per sensor:
//   {"enabled": true, "debug_every_n": 0, "debug_max_images": 200,
//    "defaults": {<params>}, "sensors": [{"sensor_id": 2, "params": {<params>}}]}
inline constexpr std::string_view kAnalysisModule = "analysis";

struct EdgeMargins {
    std::uint32_t left{0};
    std::uint32_t top{0};
    std::uint32_t right{0};
    std::uint32_t bottom{0};

    bool operator==(const EdgeMargins&) const = default;
};

struct AnalysisParams {
    // Segmentation (P60.40): HSV range, OpenCV scale (H 0..179, S and V 0..255), inclusive,
    // computed from true RGB order (eqraftvision: RGB2HSV). Mono cameras give H = S = 0.
    std::array<std::uint8_t, 3> hsvLower{60, 70, 0};
    std::array<std::uint8_t, 3> hsvUpper{105, 170, 255};
    std::uint32_t morphKernelPx{3};    // ellipse kernel for erode and dilate; 0 = none
    std::uint32_t erodeIterations{1};  // removes noise specks (eats into the edge)
    std::uint32_t dilateIterations{1}; // reconnects fragments
    // Detection runs this far around the lane ROI, so an object on the ROI edge is seen whole.
    // Only objects whose centre lies inside the ROI count; the others belong to a neighbour cup.
    EdgeMargins roiBufferPx{.left = 50, .top = 50, .right = 50, .bottom = 50};
    // Object filter
    std::uint32_t minBlobAreaPx{100}; // smaller components are ignored (noise)
    double minDiameterPx{100.0};
    double minHullAreaPx{6000.0};
    double minSolidity{0.85};   // area / hull area; background pulled in lowers it
    double maxAspectRatio{3.0}; // bounding box, either orientation
    // Size (P60.60). roiWidthMm > 0: mm per px = roiWidthMm / lane ROI width in px; otherwise
    // mmPerPx. QUICK FIX, strictly for the current test set-up: it only works because the lane
    // ROI is drawn exactly as wide as one cup (200 mm). Replaced by the G30.40 calibration
    // (P60.50); do not build on it.
    double mmPerPx{2.0};
    double roiWidthMm{200.0};

    bool operator==(const AnalysisParams&) const = default;
};

struct AnalysisConfig {
    bool enabled{true};
    std::uint32_t debugEveryN{0};      // annotated image every N frames per sensor; 0 = off
    std::uint32_t debugMaxImages{200}; // per sensor; the oldest are deleted
    AnalysisParams defaults;
    std::map<std::uint16_t, AnalysisParams> sensors; // overrides by sensor ID

    [[nodiscard]] const AnalysisParams& forSensor(std::uint16_t sensorId) const;
};

[[nodiscard]] nlohmann::json analysisSchema();
[[nodiscard]] nlohmann::json analysisDefaults(); // = AnalysisConfig{}

// ValidationFailed: schema, an HSV array without 3 values, hue > 179, lower > upper, or a
// sensor listed twice.
[[nodiscard]] Result<AnalysisConfig> analysisConfigFromJson(const nlohmann::json& config);
[[nodiscard]] nlohmann::json analysisConfigToJson(const AnalysisConfig& config);

[[nodiscard]] Result<> registerAnalysisConfig(IConfigStore& store);
// Defaults when the module is not registered (tests).
[[nodiscard]] Result<AnalysisConfig> loadAnalysisConfig(const IConfigStore& store);

} // namespace vsort::service

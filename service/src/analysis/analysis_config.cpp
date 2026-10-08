#include "analysis/analysis_config.hpp"

#include <string>
#include <utility>

namespace vsort::service {

using nlohmann::json;

namespace {

json hsvSchema() {
    return json{{"type", "array"},
                {"items", {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}}}};
}

json marginSchema() {
    const json px = {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}};
    return json{{"type", "object"},
                {"additionalProperties", false},
                {"required", json::array({"left", "top", "right", "bottom"})},
                {"properties", {{"left", px}, {"top", px}, {"right", px}, {"bottom", px}}}};
}

json paramsSchema() {
    return json{
        {"type", "object"},
        {"additionalProperties", false},
        {"required", json::array({"hsv_lower", "hsv_upper", "morph_kernel_px", "erode_iterations",
                                  "dilate_iterations", "roi_buffer_px", "min_blob_area_px",
                                  "min_diameter_px", "min_hull_area_px", "min_solidity",
                                  "max_aspect_ratio", "mm_per_px", "roi_width_mm"})},
        {"properties",
         {{"hsv_lower", hsvSchema()},
          {"hsv_upper", hsvSchema()},
          {"morph_kernel_px", {{"type", "integer"}, {"minimum", 0}, {"maximum", 51}}},
          {"erode_iterations", {{"type", "integer"}, {"minimum", 0}, {"maximum", 20}}},
          {"dilate_iterations", {{"type", "integer"}, {"minimum", 0}, {"maximum", 20}}},
          {"roi_buffer_px", marginSchema()},
          {"min_blob_area_px", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100000000}}},
          {"min_diameter_px", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 100000.0}}},
          {"min_hull_area_px", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0e9}}},
          {"min_solidity", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}},
          {"max_aspect_ratio", {{"type", "number"}, {"minimum", 1.0}, {"maximum", 100.0}}},
          {"mm_per_px", {{"type", "number"}, {"minimum", 0.0001}, {"maximum", 1000.0}}},
          {"roi_width_mm", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 100000.0}}}}}};
}

json paramsToJson(const AnalysisParams& p) {
    const auto& b = p.roiBufferPx;
    return json{{"hsv_lower", json::array({p.hsvLower[0], p.hsvLower[1], p.hsvLower[2]})},
                {"hsv_upper", json::array({p.hsvUpper[0], p.hsvUpper[1], p.hsvUpper[2]})},
                {"morph_kernel_px", p.morphKernelPx},
                {"erode_iterations", p.erodeIterations},
                {"dilate_iterations", p.dilateIterations},
                {"roi_buffer_px",
                 {{"left", b.left}, {"top", b.top}, {"right", b.right}, {"bottom", b.bottom}}},
                {"min_blob_area_px", p.minBlobAreaPx},
                {"min_diameter_px", p.minDiameterPx},
                {"min_hull_area_px", p.minHullAreaPx},
                {"min_solidity", p.minSolidity},
                {"max_aspect_ratio", p.maxAspectRatio},
                {"mm_per_px", p.mmPerPx},
                {"roi_width_mm", p.roiWidthMm}};
}

// Schema-validated input. Appends rule violations to `problems`.
AnalysisParams paramsFromJson(const json& j, const std::string& path, std::string& problems) {
    AnalysisParams p;
    const auto& lower = j.at("hsv_lower");
    const auto& upper = j.at("hsv_upper");
    if (lower.size() != 3 || upper.size() != 3) {
        problems += path + ": hsv_lower and hsv_upper need 3 values (H, S, V); ";
        return p;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        p.hsvLower.at(i) = lower.at(i).get<std::uint8_t>();
        p.hsvUpper.at(i) = upper.at(i).get<std::uint8_t>();
        if (p.hsvLower.at(i) > p.hsvUpper.at(i)) {
            problems += path + ": hsv_lower[" + std::to_string(i) + "] > hsv_upper[" +
                        std::to_string(i) + "]; ";
        }
    }
    if (p.hsvUpper[0] > 179) {
        problems += path + ": hue is 0..179 (OpenCV scale); ";
    }
    p.morphKernelPx = j.at("morph_kernel_px").get<std::uint32_t>();
    p.erodeIterations = j.at("erode_iterations").get<std::uint32_t>();
    p.dilateIterations = j.at("dilate_iterations").get<std::uint32_t>();
    const auto& b = j.at("roi_buffer_px");
    p.roiBufferPx = EdgeMargins{.left = b.at("left").get<std::uint32_t>(),
                                .top = b.at("top").get<std::uint32_t>(),
                                .right = b.at("right").get<std::uint32_t>(),
                                .bottom = b.at("bottom").get<std::uint32_t>()};
    p.minBlobAreaPx = j.at("min_blob_area_px").get<std::uint32_t>();
    p.minDiameterPx = j.at("min_diameter_px").get<double>();
    p.minHullAreaPx = j.at("min_hull_area_px").get<double>();
    p.minSolidity = j.at("min_solidity").get<double>();
    p.maxAspectRatio = j.at("max_aspect_ratio").get<double>();
    p.mmPerPx = j.at("mm_per_px").get<double>();
    p.roiWidthMm = j.at("roi_width_mm").get<double>();
    return p;
}

} // namespace

const AnalysisParams& AnalysisConfig::forSensor(std::uint16_t sensorId) const {
    const auto it = sensors.find(sensorId);
    return it == sensors.end() ? defaults : it->second;
}

json analysisSchema() {
    return json{{"type", "object"},
                {"additionalProperties", false},
                {"required", json::array({"enabled", "debug_every_n", "debug_max_images",
                                          "defaults", "sensors"})},
                {"properties",
                 {{"enabled", {{"type", "boolean"}}},
                  {"debug_every_n", {{"type", "integer"}, {"minimum", 0}, {"maximum", 1000000}}},
                  {"debug_max_images", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100000}}},
                  {"defaults", paramsSchema()},
                  {"sensors",
                   {{"type", "array"},
                    {"items",
                     {{"type", "object"},
                      {"additionalProperties", false},
                      {"required", json::array({"sensor_id", "params"})},
                      {"properties",
                       {{"sensor_id", {{"type", "integer"}, {"minimum", 1}, {"maximum", 65535}}},
                        {"params", paramsSchema()}}}}}}}}}};
}

json analysisConfigToJson(const AnalysisConfig& c) {
    json sensors = json::array();
    for (const auto& [id, params] : c.sensors) {
        sensors.push_back(json{{"sensor_id", id}, {"params", paramsToJson(params)}});
    }
    return json{{"enabled", c.enabled},
                {"debug_every_n", c.debugEveryN},
                {"debug_max_images", c.debugMaxImages},
                {"defaults", paramsToJson(c.defaults)},
                {"sensors", std::move(sensors)}};
}

json analysisDefaults() {
    return analysisConfigToJson(AnalysisConfig{});
}

Result<AnalysisConfig> analysisConfigFromJson(const json& j) {
    if (auto valid = validateJson(analysisSchema(), j); !valid) {
        return std::unexpected{std::move(valid.error())};
    }
    std::string problems;
    AnalysisConfig c;
    c.enabled = j.at("enabled").get<bool>();
    c.debugEveryN = j.at("debug_every_n").get<std::uint32_t>();
    c.debugMaxImages = j.at("debug_max_images").get<std::uint32_t>();
    c.defaults = paramsFromJson(j.at("defaults"), "$.defaults", problems);
    const auto& sensors = j.at("sensors");
    for (std::size_t i = 0; i < sensors.size(); ++i) {
        const auto& s = sensors.at(i);
        const std::string path = "$.sensors[" + std::to_string(i) + "]";
        const auto id = s.at("sensor_id").get<std::uint16_t>();
        auto params = paramsFromJson(s.at("params"), path + ".params", problems);
        if (!c.sensors.try_emplace(id, params).second) {
            problems += path + ": sensor " + std::to_string(id) + " is listed twice; ";
        }
    }
    if (!problems.empty()) {
        problems.resize(problems.size() - 2); // trailing "; "
        return makeError(Errc::ValidationFailed, std::move(problems));
    }
    return c;
}

Result<> registerAnalysisConfig(IConfigStore& store) {
    return store.registerModule(std::string{kAnalysisModule}, analysisSchema(), analysisDefaults());
}

Result<AnalysisConfig> loadAnalysisConfig(const IConfigStore& store) {
    const auto config = store.get(kAnalysisModule);
    if (!config) {
        if (config.error().code == Errc::NotFound) {
            return AnalysisConfig{};
        }
        return std::unexpected{config.error()};
    }
    return analysisConfigFromJson(*config);
}

} // namespace vsort::service

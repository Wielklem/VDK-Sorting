#include <string>
#include <utility>

#include <vsort/camera/camera_settings_config.hpp>

namespace vsort::camera {

using nlohmann::json;

namespace {

std::string triggerName(TriggerMode mode) {
    switch (mode) {
    case TriggerMode::FreeRun:
        return "free_run";
    case TriggerMode::Software:
        return "software";
    case TriggerMode::Hardware:
        break;
    }
    return "hardware";
}

TriggerMode triggerFromName(const std::string& name) {
    if (name == "free_run") {
        return TriggerMode::FreeRun;
    }
    if (name == "software") {
        return TriggerMode::Software;
    }
    return TriggerMode::Hardware;
}

} // namespace

json cameraSettingsSchema() {
    return json::parse(R"({
      "type": "object",
      "required": ["cameras"],
      "additionalProperties": false,
      "properties": {
        "cameras": {
          "type": "array",
          "items": {
            "type": "object",
            "required": ["id", "exposure_us", "gain_db", "trigger", "edge", "roi"],
            "additionalProperties": false,
            "properties": {
              "id": {"type": "integer", "minimum": 0, "maximum": 65535},
              "exposure_us": {"type": "number", "minimum": 0},
              "gain_db": {"type": "number", "minimum": 0},
              "trigger": {"type": "string", "enum": ["free_run", "software", "hardware"]},
              "edge": {"type": "string", "enum": ["rising", "falling"]},
              "roi": {
                "type": "object",
                "required": ["x", "y", "width", "height"],
                "additionalProperties": false,
                "properties": {
                  "x": {"type": "integer", "minimum": 0},
                  "y": {"type": "integer", "minimum": 0},
                  "width": {"type": "integer", "minimum": 0},
                  "height": {"type": "integer", "minimum": 0}
                }
              }
            }
          }
        }
      }
    })");
}

json cameraSettingsDefaults() {
    return json::parse(R"({"cameras": []})");
}

json cameraSettingsToJson(std::uint16_t id, const CameraSettings& settings) {
    return json{{"id", id},
                {"exposure_us", settings.exposureUs},
                {"gain_db", settings.gainDb},
                {"trigger", triggerName(settings.triggerMode)},
                {"edge", settings.triggerEdge == TriggerEdge::Falling ? "falling" : "rising"},
                {"roi", json{{"x", settings.roi.x},
                             {"y", settings.roi.y},
                             {"width", settings.roi.width},
                             {"height", settings.roi.height}}}};
}

Result<std::map<std::uint16_t, CameraSettings>> settingsFromJson(const json& config) {
    if (auto valid = validateJson(cameraSettingsSchema(), config); !valid) {
        return std::unexpected{std::move(valid.error())};
    }
    std::map<std::uint16_t, CameraSettings> out;
    for (const auto& item : config.at("cameras")) {
        CameraSettings settings;
        const auto id = item.at("id").get<std::uint16_t>();
        settings.exposureUs = item.at("exposure_us").get<double>();
        settings.gainDb = item.at("gain_db").get<double>();
        settings.triggerMode = triggerFromName(item.at("trigger").get<std::string>());
        settings.triggerEdge = item.at("edge").get<std::string>() == "falling"
                                   ? TriggerEdge::Falling
                                   : TriggerEdge::Rising;
        const auto& roi = item.at("roi");
        settings.roi = Roi{.x = roi.at("x").get<std::uint32_t>(),
                           .y = roi.at("y").get<std::uint32_t>(),
                           .width = roi.at("width").get<std::uint32_t>(),
                           .height = roi.at("height").get<std::uint32_t>()};
        if (!out.emplace(id, settings).second) {
            return makeError(Errc::ValidationFailed,
                             "camera_settings: duplicate camera ID " + std::to_string(id));
        }
    }
    return out;
}

Result<> registerCameraSettings(IConfigStore& store) {
    return store.registerModule(std::string{kCameraSettingsModule}, cameraSettingsSchema(),
                                cameraSettingsDefaults());
}

Result<std::map<std::uint16_t, CameraSettings>> loadCameraSettings(const IConfigStore& store) {
    const auto config = store.get(kCameraSettingsModule);
    if (!config) {
        return std::unexpected{config.error()};
    }
    return settingsFromJson(*config);
}

Result<> saveCameraSettings(IConfigStore& store, std::uint16_t id, const CameraSettings& settings,
                            std::string_view author) {
    auto all = loadCameraSettings(store);
    if (!all) {
        return std::unexpected{all.error()};
    }
    (*all)[id] = settings;
    json cameras = json::array();
    for (const auto& [cameraId, saved] : *all) {
        cameras.push_back(cameraSettingsToJson(cameraId, saved));
    }
    const auto version =
        store.set(kCameraSettingsModule, json{{"cameras", std::move(cameras)}}, author);
    if (!version) {
        return std::unexpected{version.error()};
    }
    return {};
}

} // namespace vsort::camera

#include <algorithm>
#include <set>
#include <utility>

#include <vsort/camera/camera_map.hpp>

namespace vsort::camera {

using nlohmann::json;

json cameraMapSchema() {
    return json::parse(R"({
      "type": "object",
      "required": ["cameras"],
      "additionalProperties": false,
      "properties": {
        "cameras": {
          "type": "array",
          "items": {
            "type": "object",
            "required": ["serial", "id"],
            "additionalProperties": false,
            "properties": {
              "serial": {"type": "string"},
              "id": {"type": "integer", "minimum": 0, "maximum": 65535}
            }
          }
        }
      }
    })");
}

json cameraMapDefaults() {
    return json::parse(R"({"cameras": []})");
}

Result<CameraMap> CameraMap::create(std::vector<CameraMapEntry> entries) {
    std::set<std::string> serials;
    std::set<std::uint16_t> ids;
    for (const auto& e : entries) {
        if (e.serial.empty()) {
            return makeError(Errc::ValidationFailed, "CameraMap: empty serial number");
        }
        if (!serials.insert(e.serial).second) {
            return makeError(Errc::ValidationFailed,
                             "CameraMap: duplicate serial '" + e.serial + "'");
        }
        if (!ids.insert(e.id).second) {
            return makeError(Errc::ValidationFailed,
                             "CameraMap: duplicate camera ID " + std::to_string(e.id));
        }
    }
    CameraMap map;
    map.entries_ = std::move(entries);
    return map;
}

Result<CameraMap> CameraMap::fromJson(const json& config) {
    if (auto valid = validateJson(cameraMapSchema(), config); !valid) {
        return std::unexpected{std::move(valid.error())};
    }
    std::vector<CameraMapEntry> entries;
    for (const auto& item : config.at("cameras")) {
        entries.push_back({.serial = item.at("serial").get<std::string>(),
                           .id = item.at("id").get<std::uint16_t>()});
    }
    return create(std::move(entries));
}

json CameraMap::toJson() const {
    json cameras = json::array();
    for (const auto& e : entries_) {
        cameras.push_back(json{{"serial", e.serial}, {"id", e.id}});
    }
    return json{{"cameras", std::move(cameras)}};
}

std::optional<std::uint16_t> CameraMap::idFor(std::string_view serial) const noexcept {
    const auto it = std::ranges::find(entries_, serial, &CameraMapEntry::serial);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->id;
}

std::optional<std::string> CameraMap::serialFor(std::uint16_t id) const {
    const auto it = std::ranges::find(entries_, id, &CameraMapEntry::id);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->serial;
}

CameraAssignment assignCameras(const CameraMap& map, const std::vector<DiscoveredCamera>& found) {
    CameraAssignment out;
    for (const auto& cam : found) {
        if (const auto id = map.idFor(cam.serial)) {
            out.mapped.push_back({.id = *id, .camera = cam});
        } else {
            out.unmappedSerials.push_back(cam.serial);
        }
    }
    std::ranges::sort(out.mapped, {}, &MappedCamera::id);
    for (const auto& e : map.entries()) {
        const bool seen = std::ranges::any_of(
            found, [&](const DiscoveredCamera& c) { return c.serial == e.serial; });
        if (!seen) {
            out.missing.push_back(e);
        }
    }
    return out;
}

Result<> registerCameraMap(IConfigStore& store) {
    return store.registerModule(std::string{kCameraMapModule}, cameraMapSchema(),
                                cameraMapDefaults());
}

Result<CameraMap> loadCameraMap(const IConfigStore& store) {
    const auto config = store.get(kCameraMapModule);
    if (!config) {
        return std::unexpected{config.error()};
    }
    return CameraMap::fromJson(*config);
}

} // namespace vsort::camera

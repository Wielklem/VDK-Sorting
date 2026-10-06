#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <vsort/camera/discovery.hpp>
#include <vsort/common/config_store.hpp>
#include <vsort/common/error.hpp>

namespace vsort::camera {

// Config module name (a-z, 0-9, '_').
inline constexpr std::string_view kCameraMapModule = "camera_map";

// One camera: hardware serial number -> logical camera ID (FrameMetadata::cameraIndex).
struct CameraMapEntry {
    std::string serial;
    std::uint16_t id{0};
};

// Serial <-> logical ID. Both are unique. Config JSON:
//   {"cameras": [{"serial": "ABC123", "id": 0}, ...]}
class CameraMap {
public:
    // ValidationFailed: schema violation, empty serial, duplicate serial or duplicate id.
    [[nodiscard]] static Result<CameraMap> fromJson(const nlohmann::json& config);
    [[nodiscard]] static Result<CameraMap> create(std::vector<CameraMapEntry> entries);

    [[nodiscard]] nlohmann::json toJson() const;

    [[nodiscard]] std::optional<std::uint16_t> idFor(std::string_view serial) const noexcept;
    [[nodiscard]] std::optional<std::string> serialFor(std::uint16_t id) const;
    [[nodiscard]] const std::vector<CameraMapEntry>& entries() const noexcept { return entries_; }

private:
    std::vector<CameraMapEntry> entries_;
};

struct MappedCamera {
    std::uint16_t id{0};
    DiscoveredCamera camera;
};

struct CameraAssignment {
    std::vector<MappedCamera> mapped;         // found and configured, sorted by id
    std::vector<std::string> unmappedSerials; // found but not in the config
    std::vector<CameraMapEntry> missing;      // in the config but not found
};

// Matches discovered cameras against the map by serial number.
[[nodiscard]] CameraAssignment assignCameras(const CameraMap& map,
                                             const std::vector<DiscoveredCamera>& found);

[[nodiscard]] nlohmann::json cameraMapSchema();
[[nodiscard]] nlohmann::json cameraMapDefaults(); // empty mapping

// Registers the module (defaults = empty mapping) and loads the validated map.
[[nodiscard]] Result<> registerCameraMap(IConfigStore& store);
[[nodiscard]] Result<CameraMap> loadCameraMap(const IConfigStore& store);

} // namespace vsort::camera

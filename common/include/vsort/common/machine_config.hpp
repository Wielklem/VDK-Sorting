#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <vsort/common/config_store.hpp>
#include <vsort/common/error.hpp>

namespace vsort {

// Config module name (a-z, 0-9, '_').
inline constexpr std::string_view kMachineModule = "machine";

// Machine model (P50.10): line -> lanes -> sensors. Config JSON:
//   {"lines": [{"id": 1, "name": "Line 1", "lanes": [{"id": 1, "name": "Lane 1", "sensors": [
//      {"id": 1, "name": "Camera 1", "kind": "camera", "camera_id": 0, "roi_id": 0,
//       "offset_cups": 0, "show_in_monitor": true,
//       "measurements": [{"key": "size_mm", "label": "Size", "unit": "mm"}]}]}]}]}
// Sensors are listed upstream first. Lane cup ID = sensor cup counter - offset_cups.
// roi_id refers to a ROI of that camera in the "rois" module; 0 = whole image.

enum class SensorKind : std::uint8_t { Camera = 1 };

struct MeasurementDef {
    std::string key; // a-z, 0-9, '_'; unique within the sensor
    std::string label;
    std::string unit; // may be empty
};

struct SensorConfig {
    std::uint16_t id{0}; // unique in the machine
    std::string name;
    SensorKind kind{SensorKind::Camera};
    std::uint16_t cameraId{0}; // logical camera ID (camera_map)
    std::uint32_t roiId{0};    // 0 = whole image
    std::uint32_t offsetCups{0};
    bool showInMonitor{true};
    std::vector<MeasurementDef> measurements;
};

struct LaneConfig {
    std::uint16_t id{0}; // unique in the machine
    std::string name;
    std::vector<SensorConfig> sensors; // upstream first, at least one
};

struct LineConfig {
    std::uint16_t id{0}; // unique in the machine
    std::string name;
    std::vector<LaneConfig> lanes; // at least one
};

// A sensor plus the lane it belongs to. Pointers stay valid while the MachineConfig lives
// and is not copied or modified.
struct SensorRef {
    const LaneConfig* lane{nullptr};
    const SensorConfig* sensor{nullptr};
};

class MachineConfig {
public:
    // ValidationFailed: schema violation or a basic rule (unique IDs, unique camera per lane,
    // offsets not decreasing along the lane, valid measurement keys, non-empty names/lists).
    // The message lists every violation as "$.path: problem".
    [[nodiscard]] static Result<MachineConfig> fromJson(const nlohmann::json& config);

    [[nodiscard]] nlohmann::json toJson() const;

    [[nodiscard]] const std::vector<LineConfig>& lines() const noexcept { return lines_; }
    [[nodiscard]] std::vector<const LaneConfig*> lanes() const; // all lanes, config order
    [[nodiscard]] const LaneConfig* findLane(std::uint16_t laneId) const noexcept;
    // All sensors that use this camera (a camera may see several lanes), config order.
    [[nodiscard]] std::vector<SensorRef> sensorsForCamera(std::uint16_t cameraId) const;

private:
    std::vector<LineConfig> lines_;
};

[[nodiscard]] nlohmann::json machineSchema();
[[nodiscard]] nlohmann::json machineDefaults(); // V1: 1 line, 1 lane, cameras 0..3, offsets 0

// Checks that every roi_id != 0 exists for that camera in the "rois" config.
// ValidationFailed lists the missing ones.
[[nodiscard]] Result<> checkRoiReferences(const MachineConfig& machine,
                                          const nlohmann::json& roiConfig);

// Registers the module with the defaults above.
[[nodiscard]] Result<> registerMachineConfig(IConfigStore& store);
[[nodiscard]] Result<MachineConfig> loadMachineConfig(const IConfigStore& store);

} // namespace vsort

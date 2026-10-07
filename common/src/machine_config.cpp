#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include <vsort/common/machine_config.hpp>

namespace vsort {
namespace {

using nlohmann::json;

bool validKey(std::string_view key) {
    return !key.empty() && std::ranges::all_of(key, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

std::string idx(std::string_view path, std::string_view member, std::size_t i) {
    return std::string{path} + "." + std::string{member} + "[" + std::to_string(i) + "]";
}

MeasurementDef parseMeasurement(const json& j) {
    return {.key = j.at("key").get<std::string>(),
            .label = j.at("label").get<std::string>(),
            .unit = j.at("unit").get<std::string>()};
}

SensorConfig parseSensor(const json& j) {
    SensorConfig s;
    s.id = j.at("id").get<std::uint16_t>();
    s.name = j.at("name").get<std::string>();
    s.kind = SensorKind::Camera; // only kind so far (schema enum)
    s.cameraId = j.at("camera_id").get<std::uint16_t>();
    s.roiId = j.at("roi_id").get<std::uint32_t>();
    s.offsetCups = j.at("offset_cups").get<std::uint32_t>();
    s.showInMonitor = j.at("show_in_monitor").get<bool>();
    for (const auto& m : j.at("measurements")) {
        s.measurements.push_back(parseMeasurement(m));
    }
    return s;
}

// Basic rules the JSON-schema subset cannot express.
void checkRules(const std::vector<LineConfig>& lines, std::vector<std::string>& errors) {
    std::set<std::uint16_t> lineIds;
    std::set<std::uint16_t> laneIds;
    std::set<std::uint16_t> sensorIds;
    for (std::size_t li = 0; li < lines.size(); ++li) {
        const auto& line = lines[li];
        const auto linePath = idx("$", "lines", li);
        if (!lineIds.insert(line.id).second) {
            errors.push_back(linePath + ".id: duplicate line ID " + std::to_string(line.id));
        }
        if (line.name.empty()) {
            errors.push_back(linePath + ".name: empty");
        }
        if (line.lanes.empty()) {
            errors.push_back(linePath + ".lanes: a line needs at least one lane");
        }
        for (std::size_t ai = 0; ai < line.lanes.size(); ++ai) {
            const auto& lane = line.lanes[ai];
            const auto lanePath = idx(linePath, "lanes", ai);
            if (!laneIds.insert(lane.id).second) {
                errors.push_back(lanePath + ".id: duplicate lane ID " + std::to_string(lane.id));
            }
            if (lane.name.empty()) {
                errors.push_back(lanePath + ".name: empty");
            }
            if (lane.sensors.empty()) {
                errors.push_back(lanePath + ".sensors: a lane needs at least one sensor");
            }
            std::set<std::uint16_t> camerasInLane;
            for (std::size_t si = 0; si < lane.sensors.size(); ++si) {
                const auto& sensor = lane.sensors[si];
                const auto sensorPath = idx(lanePath, "sensors", si);
                if (!sensorIds.insert(sensor.id).second) {
                    errors.push_back(sensorPath + ".id: duplicate sensor ID " +
                                     std::to_string(sensor.id));
                }
                if (sensor.name.empty()) {
                    errors.push_back(sensorPath + ".name: empty");
                }
                if (!camerasInLane.insert(sensor.cameraId).second) {
                    errors.push_back(sensorPath + ".camera_id: camera " +
                                     std::to_string(sensor.cameraId) + " used twice in this lane");
                }
                if (si > 0 && sensor.offsetCups < lane.sensors[si - 1].offsetCups) {
                    errors.push_back(sensorPath +
                                     ".offset_cups: smaller than the previous sensor's (sensors "
                                     "are listed upstream first)");
                }
                std::set<std::string, std::less<>> keys;
                for (std::size_t mi = 0; mi < sensor.measurements.size(); ++mi) {
                    const auto& m = sensor.measurements[mi];
                    const auto mPath = idx(sensorPath, "measurements", mi);
                    if (!validKey(m.key)) {
                        errors.push_back(mPath + ".key: '" + m.key + "' (use a-z, 0-9, '_')");
                    } else if (!keys.insert(m.key).second) {
                        errors.push_back(mPath + ".key: duplicate '" + m.key + "'");
                    }
                    if (m.label.empty()) {
                        errors.push_back(mPath + ".label: empty");
                    }
                }
            }
        }
    }
}

std::string joinErrors(const std::vector<std::string>& errors) {
    std::string message;
    for (const auto& e : errors) {
        if (!message.empty()) {
            message += "; ";
        }
        message += e;
    }
    return message;
}

} // namespace

json machineSchema() {
    return json::parse(R"({
      "type": "object",
      "required": ["lines"],
      "additionalProperties": false,
      "properties": {
        "lines": {
          "type": "array",
          "items": {
            "type": "object",
            "required": ["id", "name", "lanes"],
            "additionalProperties": false,
            "properties": {
              "id": {"type": "integer", "minimum": 1, "maximum": 65535},
              "name": {"type": "string"},
              "lanes": {
                "type": "array",
                "items": {
                  "type": "object",
                  "required": ["id", "name", "sensors"],
                  "additionalProperties": false,
                  "properties": {
                    "id": {"type": "integer", "minimum": 1, "maximum": 65535},
                    "name": {"type": "string"},
                    "sensors": {
                      "type": "array",
                      "items": {
                        "type": "object",
                        "required": ["id", "name", "kind", "camera_id", "roi_id", "offset_cups",
                                     "show_in_monitor", "measurements"],
                        "additionalProperties": false,
                        "properties": {
                          "id": {"type": "integer", "minimum": 1, "maximum": 65535},
                          "name": {"type": "string"},
                          "kind": {"type": "string", "enum": ["camera"]},
                          "camera_id": {"type": "integer", "minimum": 0, "maximum": 65535},
                          "roi_id": {"type": "integer", "minimum": 0, "maximum": 4294967295},
                          "offset_cups": {"type": "integer", "minimum": 0, "maximum": 100000},
                          "show_in_monitor": {"type": "boolean"},
                          "measurements": {
                            "type": "array",
                            "items": {
                              "type": "object",
                              "required": ["key", "label", "unit"],
                              "additionalProperties": false,
                              "properties": {
                                "key": {"type": "string"},
                                "label": {"type": "string"},
                                "unit": {"type": "string"}
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    })");
}

json machineDefaults() {
    json sensors = json::array();
    for (int i = 0; i < 4; ++i) {
        sensors.push_back(json{{"id", i + 1},
                               {"name", "Camera " + std::to_string(i + 1)},
                               {"kind", "camera"},
                               {"camera_id", i},
                               {"roi_id", 0},
                               {"offset_cups", 0},
                               {"show_in_monitor", true},
                               {"measurements", json::array()}});
    }
    json lane = {{"id", 1}, {"name", "Lane 1"}, {"sensors", std::move(sensors)}};
    json line = {{"id", 1}, {"name", "Line 1"}, {"lanes", json::array({std::move(lane)})}};
    return json{{"lines", json::array({std::move(line)})}};
}

Result<MachineConfig> MachineConfig::fromJson(const json& config) {
    if (auto valid = validateJson(machineSchema(), config); !valid) {
        return std::unexpected{std::move(valid.error())};
    }
    MachineConfig machine;
    for (const auto& jl : config.at("lines")) {
        LineConfig line{.id = jl.at("id").get<std::uint16_t>(),
                        .name = jl.at("name").get<std::string>(),
                        .lanes = {}};
        for (const auto& ja : jl.at("lanes")) {
            LaneConfig lane{.id = ja.at("id").get<std::uint16_t>(),
                            .name = ja.at("name").get<std::string>(),
                            .sensors = {}};
            for (const auto& js : ja.at("sensors")) {
                lane.sensors.push_back(parseSensor(js));
            }
            line.lanes.push_back(std::move(lane));
        }
        machine.lines_.push_back(std::move(line));
    }
    std::vector<std::string> errors;
    checkRules(machine.lines_, errors);
    if (!errors.empty()) {
        return makeError(Errc::ValidationFailed, joinErrors(errors));
    }
    return machine;
}

json MachineConfig::toJson() const {
    json lines = json::array();
    for (const auto& line : lines_) {
        json lanes = json::array();
        for (const auto& lane : line.lanes) {
            json sensors = json::array();
            for (const auto& s : lane.sensors) {
                json measurements = json::array();
                for (const auto& m : s.measurements) {
                    measurements.push_back(
                        json{{"key", m.key}, {"label", m.label}, {"unit", m.unit}});
                }
                sensors.push_back(json{{"id", s.id},
                                       {"name", s.name},
                                       {"kind", "camera"},
                                       {"camera_id", s.cameraId},
                                       {"roi_id", s.roiId},
                                       {"offset_cups", s.offsetCups},
                                       {"show_in_monitor", s.showInMonitor},
                                       {"measurements", std::move(measurements)}});
            }
            lanes.push_back(
                json{{"id", lane.id}, {"name", lane.name}, {"sensors", std::move(sensors)}});
        }
        lines.push_back(json{{"id", line.id}, {"name", line.name}, {"lanes", std::move(lanes)}});
    }
    return json{{"lines", std::move(lines)}};
}

std::vector<const LaneConfig*> MachineConfig::lanes() const {
    std::vector<const LaneConfig*> out;
    for (const auto& line : lines_) {
        for (const auto& lane : line.lanes) {
            out.push_back(&lane);
        }
    }
    return out;
}

const LaneConfig* MachineConfig::findLane(std::uint16_t laneId) const noexcept {
    for (const auto& line : lines_) {
        for (const auto& lane : line.lanes) {
            if (lane.id == laneId) {
                return &lane;
            }
        }
    }
    return nullptr;
}

std::vector<SensorRef> MachineConfig::sensorsForCamera(std::uint16_t cameraId) const {
    std::vector<SensorRef> out;
    for (const auto& line : lines_) {
        for (const auto& lane : line.lanes) {
            for (const auto& sensor : lane.sensors) {
                if (sensor.cameraId == cameraId) {
                    out.push_back({.lane = &lane, .sensor = &sensor});
                }
            }
        }
    }
    return out;
}

Result<> checkRoiReferences(const MachineConfig& machine, const json& roiConfig) {
    std::set<std::pair<std::uint16_t, std::uint32_t>> known;
    if (roiConfig.contains("cameras") && roiConfig.at("cameras").is_array()) {
        for (const auto& cam : roiConfig.at("cameras")) {
            const auto cameraId = cam.value("camera_id", std::uint16_t{0});
            for (const auto& roi : cam.value("rois", json::array())) {
                known.emplace(cameraId, roi.value("id", std::uint32_t{0}));
            }
        }
    }
    std::vector<std::string> errors;
    for (const auto* lane : machine.lanes()) {
        for (const auto& s : lane->sensors) {
            if (s.roiId != 0 && !known.contains({s.cameraId, s.roiId})) {
                errors.push_back("sensor " + std::to_string(s.id) + " (lane " +
                                 std::to_string(lane->id) + "): ROI " + std::to_string(s.roiId) +
                                 " not found for camera " + std::to_string(s.cameraId));
            }
        }
    }
    if (!errors.empty()) {
        return makeError(Errc::ValidationFailed, joinErrors(errors));
    }
    return {};
}

Result<> registerMachineConfig(IConfigStore& store) {
    return store.registerModule(std::string{kMachineModule}, machineSchema(), machineDefaults());
}

Result<MachineConfig> loadMachineConfig(const IConfigStore& store) {
    const auto config = store.get(kMachineModule);
    if (!config) {
        return std::unexpected{config.error()};
    }
    return MachineConfig::fromJson(*config);
}

} // namespace vsort

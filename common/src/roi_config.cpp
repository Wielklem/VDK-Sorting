#include <string>

#include <vsort/common/roi_config.hpp>

namespace vsort {

using nlohmann::json;

json roiSchema() {
    return json::parse(R"({
      "type": "object",
      "required": ["cameras"],
      "additionalProperties": false,
      "properties": {
        "cameras": {
          "type": "array",
          "items": {
            "type": "object",
            "required": ["camera_id", "rois"],
            "additionalProperties": false,
            "properties": {
              "camera_id": {"type": "integer", "minimum": 0, "maximum": 65535},
              "rois": {
                "type": "array",
                "items": {
                  "type": "object",
                  "required": ["id", "name", "x", "y", "width", "height"],
                  "additionalProperties": false,
                  "properties": {
                    "id": {"type": "integer", "minimum": 1},
                    "name": {"type": "string"},
                    "x": {"type": "number", "minimum": 0, "maximum": 1},
                    "y": {"type": "number", "minimum": 0, "maximum": 1},
                    "width": {"type": "number", "minimum": 0.001, "maximum": 1},
                    "height": {"type": "number", "minimum": 0.001, "maximum": 1}
                  }
                }
              }
            }
          }
        }
      }
    })");
}

json roiDefaults() {
    return json::parse(R"({"cameras": []})");
}

Result<> registerRoiConfig(IConfigStore& store) {
    return store.registerModule(std::string{kRoiModule}, roiSchema(), roiDefaults());
}

} // namespace vsort

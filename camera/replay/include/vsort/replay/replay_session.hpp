#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <vsort/common/error.hpp>

namespace vsort::replay {

// One camera of a recorded session (an entry of session.json).
struct SessionCamera {
    std::string serial;
    std::string model;
    std::uint16_t index{0}; // logical camera ID at recording time
    std::string file;       // recording file name (camNN.vrec), next to session.json
};

// Reads <sessionDir>/session.json. Sorted by index.
// NotFound: no session.json. ParseError: damaged file or an entry without serial or index.
[[nodiscard]] Result<std::vector<SessionCamera>>
listSessionCameras(const std::filesystem::path& sessionDir);

} // namespace vsort::replay

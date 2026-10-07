#include <algorithm>
#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

#include <vsort/replay/replay_session.hpp>

namespace vsort::replay {

Result<std::vector<SessionCamera>> listSessionCameras(const std::filesystem::path& sessionDir) {
    const std::filesystem::path path = sessionDir / "session.json";
    std::ifstream in{path};
    if (!in) {
        return makeError(Errc::NotFound, "cannot open '" + path.string() + "'");
    }
    try {
        const nlohmann::json session = nlohmann::json::parse(in, nullptr, false);
        if (session.is_discarded() || !session.is_object() || !session.contains("cameras") ||
            !session["cameras"].is_array()) {
            return makeError(Errc::ParseError, "'" + path.string() + "': not a session file");
        }
        std::vector<SessionCamera> out;
        for (const auto& entry : session["cameras"]) {
            if (!entry.is_object()) {
                return makeError(Errc::ParseError, "session.json: bad camera entry");
            }
            SessionCamera camera;
            camera.serial = entry.value("serial", std::string{});
            camera.model = entry.value("model", std::string{});
            camera.index = entry.at("index").get<std::uint16_t>();
            if (camera.serial.empty()) {
                return makeError(Errc::ParseError, "session.json: camera without serial");
            }
            out.push_back(std::move(camera));
        }
        std::ranges::sort(out, {}, &SessionCamera::index);
        return out;
    } catch (const nlohmann::json::exception& ex) {
        return makeError(Errc::ParseError, std::string{"session.json: "} + ex.what());
    }
}

} // namespace vsort::replay

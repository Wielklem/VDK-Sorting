#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include <vsort/replay/replay_camera.hpp>
#include <vsort/replay/replay_session.hpp>

#include "camera/camera_backend.hpp"

namespace vsort::service {

// Plays a recorded session as live cameras. The camera IDs are the recorded indices; the config
// store is not used (persistent() is false). All cameras share one session clock, so they keep
// their recorded time relation (REPLAY ONLY, see ReplayClock; the machine uses the encoder).
class ReplayBackend final : public ICameraBackend {
public:
    // NotFound / ParseError: session.json missing or damaged. NotFound: no cameras in it.
    [[nodiscard]] static Result<std::unique_ptr<ReplayBackend>> create(replay::ReplayConfig config);

    [[nodiscard]] std::string_view name() const noexcept override { return "replay"; }
    [[nodiscard]] Result<std::vector<camera::DiscoveredCamera>> discover() override;
    [[nodiscard]] std::unique_ptr<camera::ICamera> create() override;
    [[nodiscard]] bool persistent() const noexcept override { return false; }
    [[nodiscard]] std::vector<camera::CameraMapEntry> fixedMap() const override;

private:
    ReplayBackend(replay::ReplayConfig config, std::vector<replay::SessionCamera> cameras);

    replay::ReplayConfig config_;
    std::vector<replay::SessionCamera> cameras_;
};

} // namespace vsort::service

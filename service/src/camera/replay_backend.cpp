#include "camera/replay_backend.hpp"

#include <utility>

#include <spdlog/spdlog.h>

#include <vsort/replay/replay_clock.hpp>

namespace vsort::service {

ReplayBackend::ReplayBackend(replay::ReplayConfig config,
                             std::vector<replay::SessionCamera> cameras)
    : config_{std::move(config)}
    , cameras_{std::move(cameras)} {}

Result<std::unique_ptr<ReplayBackend>> ReplayBackend::create(replay::ReplayConfig config) {
    auto cameras = replay::listSessionCameras(config.sessionDir);
    if (!cameras) {
        return std::unexpected{cameras.error()};
    }
    if (cameras->empty()) {
        return makeError(Errc::NotFound,
                         "session '" + config.sessionDir.string() + "' has no cameras");
    }
    // REPLAY ONLY: one clock for all cameras of the session, so they keep their recorded time
    // relation (start offsets, each sensor's own frame timing during ramps) and loop together.
    // The machine (P130) links every cup to an encoder value instead; no clock is needed there.
    auto clock = replay::ReplayClock::forSession(config.sessionDir, config.speed);
    if (!clock) {
        return std::unexpected{clock.error()};
    }
    spdlog::info("replay: shared session clock for all cameras (replay only), loop {:.3f} s",
                 static_cast<double>((*clock)->loopPeriodNs()) / 1e9);
    config.clock = std::move(*clock);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,modernize-make-unique): private constructor
    return std::unique_ptr<ReplayBackend>{
        new ReplayBackend{std::move(config), std::move(*cameras)}};
}

Result<std::vector<camera::DiscoveredCamera>> ReplayBackend::discover() {
    std::vector<camera::DiscoveredCamera> out;
    for (const auto& cam : cameras_) {
        camera::DiscoveredCamera found;
        found.serial = cam.serial;
        found.model = cam.model;
        out.push_back(std::move(found));
    }
    return out;
}

std::unique_ptr<camera::ICamera> ReplayBackend::create() {
    return std::make_unique<replay::ReplayCamera>(config_);
}

std::vector<camera::CameraMapEntry> ReplayBackend::fixedMap() const {
    std::vector<camera::CameraMapEntry> out;
    for (const auto& cam : cameras_) {
        out.push_back({.serial = cam.serial, .id = cam.index});
    }
    return out;
}

} // namespace vsort::service

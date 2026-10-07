#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vsort/common/module.hpp>

#include "camera/camera_manager.hpp"
#include "ipc/camera_access.hpp"

namespace vsort::service {

// What the IPC server sees of the camera manager.
class ManagerCameraAccess final : public ICameraAccess {
public:
    explicit ManagerCameraAccess(std::shared_ptr<CameraManager> manager)
        : manager_{std::move(manager)} {}

    [[nodiscard]] std::vector<CameraListEntry> list() const override { return manager_->list(); }
    [[nodiscard]] Result<camera::CameraSettings> settings(std::uint16_t id) const override {
        return manager_->settings(id);
    }
    [[nodiscard]] Result<> apply(std::uint16_t id,
                                 const camera::CameraSettings& settings) override {
        return manager_->apply(id, settings);
    }

private:
    std::shared_ptr<CameraManager> manager_;
};

// Module "camera": starts after "ipc" and stops before it, so no frame reaches a stopped hub.
class CameraModule final : public IModule {
public:
    explicit CameraModule(std::shared_ptr<CameraManager> manager)
        : manager_{std::move(manager)} {}

    [[nodiscard]] std::string_view name() const noexcept override { return "camera"; }
    [[nodiscard]] std::vector<std::string> dependencies() const override { return {"ipc"}; }
    [[nodiscard]] Result<> init(ModuleContext& /*context*/) override { return {}; }
    [[nodiscard]] Result<> start() override { return manager_->start(); }
    void stop() noexcept override { manager_->stop(); }
    [[nodiscard]] Health health() const override { return manager_->health(); }

private:
    std::shared_ptr<CameraManager> manager_;
};

} // namespace vsort::service

#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include <vsort/camera/daheng.hpp>

#include "camera/camera_backend.hpp"

namespace vsort::service {

// Real Daheng cameras (USB3 and GigE). Only built with VSORT_WITH_GALAXY.
class DahengBackend final : public ICameraBackend {
public:
    explicit DahengBackend(camera::DahengOptions options = {});

    [[nodiscard]] std::string_view name() const noexcept override { return "daheng"; }
    [[nodiscard]] Result<std::vector<camera::DiscoveredCamera>> discover() override;
    [[nodiscard]] std::unique_ptr<camera::ICamera> create() override;

private:
    camera::DahengOptions options_;
    std::unique_ptr<camera::ICameraDiscovery> discovery_;
};

} // namespace vsort::service

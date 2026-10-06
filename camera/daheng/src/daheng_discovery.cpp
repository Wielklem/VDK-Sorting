#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <vsort/camera/daheng.hpp>

#include "galaxy_lib.hpp"

namespace vsort::camera {

namespace {

class DahengDiscovery final : public ICameraDiscovery {
public:
    [[nodiscard]] Result<std::vector<DiscoveredCamera>> discover() override {
        auto lib = detail::GalaxyLib::acquire();
        if (!lib) {
            return std::unexpected<Error>{lib.error()};
        }

        const auto lock = detail::lockDeviceList();
        const auto count = detail::updateDeviceList();
        if (!count) {
            return std::unexpected<Error>{count.error()};
        }

        std::vector<DiscoveredCamera> found;
        if (*count == 0) {
            return found;
        }

        std::vector<GX_DEVICE_BASE_INFO> infos(*count);
        std::size_t bytes = infos.size() * sizeof(GX_DEVICE_BASE_INFO);
        if (const GX_STATUS status = GXGetAllDeviceBaseInfo(infos.data(), &bytes);
            status != GX_STATUS_SUCCESS) {
            return detail::fail(status, "GXGetAllDeviceBaseInfo");
        }
        const std::size_t valid = std::min(infos.size(), bytes / sizeof(GX_DEVICE_BASE_INFO));

        for (std::size_t i = 0; i < valid; ++i) {
            const GX_DEVICE_BASE_INFO& info = infos[i];
            DiscoveredCamera cam;
            cam.serial = detail::fromChars(info.szSN);
            cam.model = detail::fromChars(info.szModelName);
            if (info.deviceClass == GX_DEVICE_CLASS_U3V) {
                cam.transport = Transport::Usb3;
            } else if (info.deviceClass == GX_DEVICE_CLASS_GEV) {
                cam.transport = Transport::GigE;
                GX_DEVICE_IP_INFO ip{};
                // The SDK counts devices from 1, in the same order as the base info list.
                if (GXGetDeviceIPInfo(static_cast<std::uint32_t>(i + 1), &ip) ==
                    GX_STATUS_SUCCESS) {
                    cam.mac = detail::fromChars(ip.szMAC);
                    cam.ip = detail::fromChars(ip.szIP);
                    cam.subnetMask = detail::fromChars(ip.szSubNetMask);
                    cam.gateway = detail::fromChars(ip.szGateWay);
                    cam.nicIp = detail::fromChars(ip.szNICIP);
                    cam.nicMask = detail::fromChars(ip.szNICSubNetMask);
                }
            } else {
                continue; // USB2, CXP, smart cameras: not used
            }
            found.push_back(std::move(cam));
        }

        std::ranges::sort(found, {}, &DiscoveredCamera::serial);
        return found;
    }
};

} // namespace

std::unique_ptr<ICameraDiscovery> makeDahengDiscovery() {
    return std::make_unique<DahengDiscovery>();
}

} // namespace vsort::camera

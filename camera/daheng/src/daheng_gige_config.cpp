#include <cstdint>
#include <memory>
#include <string>

#include <vsort/camera/daheng.hpp>

#include "galaxy_lib.hpp"

namespace vsort::camera {

namespace {

class DahengGigEConfigurator final : public IGigEConfigurator {
public:
    [[nodiscard]] Result<> setIp(const GigEIpRequest& request) override {
        if (const auto valid = validateIpRequest(request); !valid) {
            return valid;
        }
        const auto wanted = normalizeMac(request.mac);

        auto lib = detail::GalaxyLib::acquire();
        if (!lib) {
            return std::unexpected<Error>{lib.error()};
        }
        const auto lock = detail::lockDeviceList();
        const auto count = detail::updateDeviceList();
        if (!count) {
            return std::unexpected<Error>{count.error()};
        }

        // Use the SDK's own MAC string: its separator style may differ from the user's input.
        std::string sdkMac;
        for (std::uint32_t index = 1; index <= *count; ++index) {
            GX_DEVICE_IP_INFO info{};
            if (GXGetDeviceIPInfo(index, &info) != GX_STATUS_SUCCESS) {
                continue; // not a GigE device
            }
            const std::string mac = detail::fromChars(info.szMAC);
            if (normalizeMac(mac) == wanted) {
                sdkMac = mac;
                break;
            }
        }
        if (sdkMac.empty()) {
            return makeError(Errc::NotFound, "no GigE camera with MAC " + request.mac);
        }

        const std::string gateway = request.gateway.empty() ? "0.0.0.0" : request.gateway;
        const GX_STATUS status =
            request.persistent
                ? GXGigEIpConfiguration(sdkMac.c_str(), GX_IP_CONFIGURE_STATIC_IP,
                                        request.ip.c_str(), request.subnetMask.c_str(),
                                        gateway.c_str(), "")
                : GXGigEForceIp(sdkMac.c_str(), request.ip.c_str(), request.subnetMask.c_str(),
                                gateway.c_str());
        if (status != GX_STATUS_SUCCESS) {
            return detail::fail(status,
                                request.persistent ? "GXGigEIpConfiguration" : "GXGigEForceIp");
        }
        return {};
    }
};

} // namespace

std::unique_ptr<IGigEConfigurator> makeDahengGigEConfigurator() {
    return std::make_unique<DahengGigEConfigurator>();
}

} // namespace vsort::camera

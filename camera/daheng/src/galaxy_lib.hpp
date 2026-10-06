#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <GxIAPI.h>

#include <vsort/common/error.hpp>

namespace vsort::camera::detail {

// Process-wide GXInitLib/GXCloseLib, reference counted. Hold the shared_ptr while using the SDK.
class GalaxyLib {
public:
    [[nodiscard]] static Result<std::shared_ptr<GalaxyLib>> acquire();

    ~GalaxyLib();
    GalaxyLib(const GalaxyLib&) = delete;
    GalaxyLib& operator=(const GalaxyLib&) = delete;
    GalaxyLib(GalaxyLib&&) = delete;
    GalaxyLib& operator=(GalaxyLib&&) = delete;

private:
    GalaxyLib() = default;
};

// SDK status -> Error, with the SDK's last-error text appended when available.
[[nodiscard]] Error toError(GX_STATUS status, std::string_view what);
[[nodiscard]] std::unexpected<Error> fail(GX_STATUS status, std::string_view what);

// The SDK keeps one global device list. Hold this lock around updateDeviceList() and
// everything that reads the list or opens a device from it.
[[nodiscard]] std::unique_lock<std::mutex> lockDeviceList();

// Scans USB3 and GigE. Returns the number of devices found. Caller holds lockDeviceList().
[[nodiscard]] Result<std::uint32_t> updateDeviceList();

// SDK strings are fixed char arrays that may not be NUL-terminated.
template <std::size_t N>
[[nodiscard]] std::string fromChars(const char (&array)[N]) {
    const std::string_view view{array, N};
    return std::string{view.substr(0, view.find('\0'))};
}

} // namespace vsort::camera::detail

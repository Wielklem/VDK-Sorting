#include "galaxy_lib.hpp"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace vsort::camera::detail {

namespace {

constexpr std::uint32_t kEnumerateTimeoutMs = 1000;

std::mutex& initMutex() {
    static std::mutex mutex;
    return mutex;
}

} // namespace

Result<std::shared_ptr<GalaxyLib>> GalaxyLib::acquire() {
    static std::weak_ptr<GalaxyLib> instance;
    const std::scoped_lock lock{initMutex()};
    if (auto existing = instance.lock()) {
        return existing;
    }
    if (const GX_STATUS status = GXInitLib(); status != GX_STATUS_SUCCESS) {
        return fail(status, "GXInitLib");
    }
    std::shared_ptr<GalaxyLib> lib{new GalaxyLib{}};
    instance = lib;
    return lib;
}

GalaxyLib::~GalaxyLib() {
    const std::scoped_lock lock{initMutex()};
    static_cast<void>(GXCloseLib());
}

Error toError(GX_STATUS status, std::string_view what) {
    Errc code = Errc::DeviceError;
    switch (status) {
    case GX_STATUS_NOT_FOUND_DEVICE:
        code = Errc::NotFound;
        break;
    case GX_STATUS_TIMEOUT:
        code = Errc::Timeout;
        break;
    case GX_STATUS_INVALID_PARAMETER:
        code = Errc::InvalidArgument;
        break;
    case GX_STATUS_INVALID_ACCESS:
        code = Errc::PermissionDenied;
        break;
    default:
        break;
    }

    std::string message{what};
    message += " failed (SDK status ";
    message += std::to_string(status);
    message += ')';

    std::array<char, 512> text{};
    std::size_t size = text.size();
    GX_STATUS last = GX_STATUS_SUCCESS;
    if (GXGetLastError(&last, text.data(), &size) == GX_STATUS_SUCCESS) {
        text.back() = '\0';
        if (text.front() != '\0') {
            message += ": ";
            message += text.data();
        }
    }
    return Error{.code = code, .message = std::move(message)};
}

std::unexpected<Error> fail(GX_STATUS status, std::string_view what) {
    return std::unexpected<Error>{toError(status, what)};
}

std::unique_lock<std::mutex> lockDeviceList() {
    static std::mutex mutex;
    return std::unique_lock{mutex};
}

Result<std::uint32_t> updateDeviceList() {
    const auto types =
        static_cast<std::uint64_t>(GX_TL_TYPE_GEV) | static_cast<std::uint64_t>(GX_TL_TYPE_U3V);
    std::uint32_t count = 0;
    if (const GX_STATUS status = GXUpdateAllDeviceListEx(types, &count, kEnumerateTimeoutMs);
        status != GX_STATUS_SUCCESS) {
        return fail(status, "GXUpdateAllDeviceListEx");
    }
    return count;
}

Result<GX_DEVICE_CLASS> deviceClassOf(std::string_view serial, std::uint32_t count) {
    if (count == 0) {
        return makeError(Errc::NotFound, "no devices");
    }
    std::vector<GX_DEVICE_BASE_INFO> infos(count);
    std::size_t bytes = infos.size() * sizeof(GX_DEVICE_BASE_INFO);
    if (const GX_STATUS status = GXGetAllDeviceBaseInfo(infos.data(), &bytes);
        status != GX_STATUS_SUCCESS) {
        return fail(status, "GXGetAllDeviceBaseInfo");
    }
    const std::size_t valid = std::min(infos.size(), bytes / sizeof(GX_DEVICE_BASE_INFO));
    for (std::size_t i = 0; i < valid; ++i) {
        if (fromChars(infos[i].szSN) == serial) {
            return infos[i].deviceClass;
        }
    }
    return makeError(Errc::NotFound, std::string{serial} + " not in the device list");
}

} // namespace vsort::camera::detail

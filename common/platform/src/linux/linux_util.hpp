#pragma once
// Internal helpers for M15.10. Never included outside common/platform/src/linux/.

#include <cerrno>
#include <cstdlib>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <vsort/common/error.hpp>

namespace vsort::platform::detail {

[[nodiscard]] inline Errc errcFromErrno(int err) noexcept {
    switch (err) {
        case EPERM:
        case EACCES: return Errc::PermissionDenied;
        case ENOENT: return Errc::NotFound;
        case EEXIST: return Errc::AlreadyExists;
        case EINVAL: return Errc::InvalidArgument;
        default:     return Errc::IoError;
    }
}

// "<what>: <OS message>" with the matching Errc.
[[nodiscard]] inline std::unexpected<Error> sysError(std::string_view what, int err) {
    std::string msg{what};
    msg += ": ";
    msg += std::system_category().message(err);
    return makeError(errcFromErrno(err), std::move(msg));
}

// Empty when unset. Only read at startup, before worker threads exist.
[[nodiscard]] inline std::string envString(const char* name) {
    const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe)
    return value != nullptr ? std::string{value} : std::string{};
}

} // namespace vsort::platform::detail

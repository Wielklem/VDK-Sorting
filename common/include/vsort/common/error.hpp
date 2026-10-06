#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace vsort {

enum class Errc : std::uint8_t {
    InvalidArgument = 1,
    NotFound,
    AlreadyExists,
    IoError,
    Timeout,
    NotSupported,
    ParseError,
    ValidationFailed,
    DeviceError,
    Internal,
    PermissionDenied,  // appended: existing values stay stable
};

[[nodiscard]] std::string_view toString(Errc code) noexcept;

struct Error {
    Errc code{Errc::Internal};
    std::string message;

    // "<code>: <message>"
    [[nodiscard]] std::string what() const;
};

template <typename T = void>
using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> makeError(Errc code, std::string message) {
    return std::unexpected<Error>{Error{.code = code, .message = std::move(message)}};
}

} // namespace vsort

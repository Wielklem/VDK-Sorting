#include <vsort/common/error.hpp>

namespace vsort {

std::string_view toString(Errc code) noexcept {
    switch (code) {
        case Errc::InvalidArgument:  return "invalid_argument";
        case Errc::NotFound:         return "not_found";
        case Errc::AlreadyExists:    return "already_exists";
        case Errc::IoError:          return "io_error";
        case Errc::Timeout:          return "timeout";
        case Errc::NotSupported:     return "not_supported";
        case Errc::ParseError:       return "parse_error";
        case Errc::ValidationFailed: return "validation_failed";
        case Errc::DeviceError:      return "device_error";
        case Errc::Internal:         return "internal";
    }
    return "unknown";
}

std::string Error::what() const {
    std::string out{toString(code)};
    out += ": ";
    out += message;
    return out;
}

} // namespace vsort

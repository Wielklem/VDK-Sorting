#include <vsort/common/timestamp.hpp>

#include <format>

namespace vsort {

Timestamp Timestamp::now() noexcept {
    return Timestamp{std::chrono::duration_cast<duration>(
        std::chrono::steady_clock::now().time_since_epoch())};
}

WallTime WallTime::now() noexcept {
    return WallTime{std::chrono::duration_cast<duration>(
        std::chrono::system_clock::now().time_since_epoch())};
}

std::string WallTime::toIso8601() const {
    const std::chrono::sys_time<std::chrono::microseconds> tp{
        std::chrono::duration_cast<std::chrono::microseconds>(ns_)};
    return std::format("{:%FT%T}Z", tp);
}

} // namespace vsort

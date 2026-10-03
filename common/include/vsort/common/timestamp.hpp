#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <string>

namespace vsort {

// Monotonic time (steady_clock). Only valid within one process run.
class Timestamp {
public:
    using duration = std::chrono::nanoseconds;

    constexpr Timestamp() noexcept = default;
    constexpr explicit Timestamp(duration sinceEpoch) noexcept : ns_{sinceEpoch} {}

    [[nodiscard]] static Timestamp now() noexcept;

    [[nodiscard]] constexpr duration sinceEpoch() const noexcept { return ns_; }
    [[nodiscard]] constexpr std::int64_t ns() const noexcept { return static_cast<std::int64_t>(ns_.count()); }

    constexpr auto operator<=>(const Timestamp&) const noexcept = default;
    constexpr duration operator-(const Timestamp& other) const noexcept { return ns_ - other.ns_; }
    constexpr Timestamp operator+(duration d) const noexcept { return Timestamp{ns_ + d}; }

private:
    duration ns_{0};
};

// Wall-clock time, UTC, nanoseconds since the Unix epoch. For storage, logs, reports.
class WallTime {
public:
    using duration = std::chrono::nanoseconds;

    constexpr WallTime() noexcept = default;
    constexpr explicit WallTime(duration sinceUnixEpoch) noexcept : ns_{sinceUnixEpoch} {}

    [[nodiscard]] static WallTime now() noexcept;

    [[nodiscard]] constexpr duration sinceEpoch() const noexcept { return ns_; }
    [[nodiscard]] constexpr std::int64_t ns() const noexcept { return static_cast<std::int64_t>(ns_.count()); }

    // e.g. 2026-10-03T12:34:56.123456Z
    [[nodiscard]] std::string toIso8601() const;

    constexpr auto operator<=>(const WallTime&) const noexcept = default;

private:
    duration ns_{0};
};

} // namespace vsort

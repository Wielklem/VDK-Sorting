#pragma once

#include <cstdint>
#include <string_view>

#include <vsort/camera/camera.hpp>

namespace vsort::camera {

// GenICam feature values for one trigger setup. Empty source/activation = leave unchanged.
struct TriggerPlan {
    bool triggerOn{false};
    std::string_view source;     // "Line0", "Software"
    std::string_view activation; // "RisingEdge", "FallingEdge"
};

[[nodiscard]] constexpr TriggerPlan triggerPlan(TriggerMode mode, TriggerEdge edge) noexcept {
    switch (mode) {
    case TriggerMode::FreeRun:
        return {};
    case TriggerMode::Software:
        return {.triggerOn = true, .source = "Software", .activation = {}};
    case TriggerMode::Hardware:
        return {.triggerOn = true,
                .source = "Line0",
                .activation = edge == TriggerEdge::Rising ? "RisingEdge" : "FallingEdge"};
    }
    return {};
}

// Device clock ticks to nanoseconds. 0 if the tick frequency is unknown (0).
// Exact for 1 GHz clocks; otherwise within 1 ns.
[[nodiscard]] constexpr std::uint64_t ticksToNs(std::uint64_t ticks,
                                                std::uint64_t tickHz) noexcept {
    constexpr std::uint64_t kNsPerSecond = 1'000'000'000;
    if (tickHz == 0) {
        return 0;
    }
    if (tickHz == kNsPerSecond) {
        return ticks;
    }
    const std::uint64_t seconds = ticks / tickHz;
    const std::uint64_t rest = ticks % tickHz;
    const auto restNs = static_cast<std::uint64_t>(static_cast<long double>(rest) * 1e9L /
                                                   static_cast<long double>(tickHz));
    return seconds * kNsPerSecond + restNs;
}

} // namespace vsort::camera

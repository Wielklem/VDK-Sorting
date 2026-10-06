#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <vsort/common/types.hpp>

namespace vsort::camera {

enum class CameraState : std::uint8_t {
    Closed = 1,
    Open,         // open, not streaming
    Streaming,    // grabbing
    Reconnecting, // connection lost, trying to restore it
};

// Counters since open(). They are not reset by a reconnect.
// framesDropped and framesMissed can overlap: a frame dropped in the adapter also leaves a gap.
struct CameraHealth {
    CameraState state{CameraState::Closed};
    std::uint64_t framesReceived{0};
    std::uint64_t framesDropped{0}; // lost inside the adapter (incomplete, pool full, ...)
    std::uint64_t idGaps{0};        // gaps in the frame ID sequence
    std::uint64_t framesMissed{0};  // frames missing according to those gaps
    std::uint64_t idResets{0};      // frame ID repeated or went backwards
    std::uint64_t reconnectAttempts{0};
    std::uint64_t reconnects{0}; // successful reconnects
    std::string lastError;       // empty = none
};

// Frame IDs from the camera count +1 per frame. The first frame after reset() sets the baseline.
class FrameIdTracker {
public:
    enum class Kind : std::uint8_t { First, InOrder, Gap, Reset };
    struct Observation {
        Kind kind{Kind::First};
        std::uint64_t missed{0}; // only for Gap
    };

    [[nodiscard]] Observation observe(FrameId id) noexcept {
        const std::uint64_t value = id.value();
        const std::optional<std::uint64_t> previous = last_;
        last_ = value;
        if (!previous) {
            return {Kind::First, 0};
        }
        if (value > *previous) {
            const std::uint64_t step = value - *previous;
            return step == 1 ? Observation{Kind::InOrder, 0} : Observation{Kind::Gap, step - 1};
        }
        return {Kind::Reset, 0}; // repeated or backwards: the camera restarted its counter
    }

    void reset() noexcept { last_.reset(); }

private:
    std::optional<std::uint64_t> last_;
};

// Doubling delay between reconnect attempts, capped.
class ReconnectBackoff {
public:
    using duration = std::chrono::milliseconds;

    ReconnectBackoff(duration initial, duration max) noexcept
        : initial_{std::max(initial, duration{1})}
        , max_{std::max(max, initial_)}
        , current_{initial_} {}

    [[nodiscard]] duration next() noexcept {
        const duration delay = current_;
        current_ = std::min(current_ * 2, max_);
        return delay;
    }

    void reset() noexcept { current_ = initial_; }

private:
    duration initial_;
    duration max_;
    duration current_;
};

} // namespace vsort::camera

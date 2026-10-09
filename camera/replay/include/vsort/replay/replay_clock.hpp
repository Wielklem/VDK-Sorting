#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>

#include <vsort/common/error.hpp>

namespace vsort::replay {

// REPLAY ONLY: one play clock for all cameras of a recorded session.
//
// Why: a replay has no machine position. Each camera used to play from its own first frame with
// its own loop period, so the cameras drifted apart by the difference on every pass and the
// tracker attached their images to the wrong cups. On the machine (P130) every cup is linked to
// an encoder value; this clock plays no part there.
//
// What it does: every frame plays at its recorded host time relative to the earliest frame of
// the whole session. The recorded relation between the cameras stays as it was: start offsets,
// and during ramps each camera's own frame timing (each camera triggers at its own place in the
// cup window, so at a changing speed its intervals differ from the others'). Nothing is
// resampled or aligned. All cameras loop with one period.
//
// The first camera that starts sets media time 0, after a short start delay so cameras started
// together all play from the beginning. A camera started later joins the running clock and skips
// the frames that are already past (a frame-ID gap, as with a live camera that starts late).
// When no camera plays any more, the next start begins at 0 again. Thread-safe.
class ReplayClock {
public:
    using Clock = std::chrono::steady_clock;

    // Start delay of a session clock: cameras started within it play from the first frame.
    static constexpr std::chrono::milliseconds kSessionStartDelay{500};

    // firstNs: earliest recorded host time. loopPeriodNs > 0. speed finite and > 0.
    ReplayClock(std::int64_t firstNs, std::int64_t loopPeriodNs, double speed,
                std::chrono::nanoseconds startDelay = {});

    // Reads the record headers of every camera in <sessionDir> (no pixel data). Loop period:
    // span of the whole session plus the mean frame interval. Recordings without frames are
    // skipped. InvalidArgument: bad speed. NotFound / ParseError: as listSessionCameras;
    // NotFound also when the session has no frames at all.
    [[nodiscard]] static Result<std::shared_ptr<ReplayClock>>
    forSession(const std::filesystem::path& sessionDir, double speed);

    [[nodiscard]] std::int64_t firstNs() const noexcept { return firstNs_; }
    [[nodiscard]] std::int64_t loopPeriodNs() const noexcept { return loopPeriodNs_; }

    // A camera starts / stops playing. attach() returns the media time to play from (>= 0).
    [[nodiscard]] std::int64_t attach();
    void detach() noexcept;

    // Wall time at which media time `mediaNs` is due.
    [[nodiscard]] Clock::time_point dueTime(std::int64_t mediaNs) const;

    // Changes the rate for every camera on this clock, without a jump in media time.
    // The caller validates (finite, > 0).
    void setSpeed(double speed);
    [[nodiscard]] double speed() const;

private:
    [[nodiscard]] std::int64_t mediaAt(Clock::time_point t) const; // mutex_ held

    std::int64_t firstNs_;
    std::int64_t loopPeriodNs_;
    std::chrono::nanoseconds startDelay_;
    mutable std::mutex mutex_;
    double speed_;
    Clock::time_point anchorWall_;
    std::int64_t anchorMediaNs_{0};
    std::uint32_t attached_{0};
};

} // namespace vsort::replay

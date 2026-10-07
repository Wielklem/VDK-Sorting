#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/camera/health.hpp>
#include <vsort/common/timestamp.hpp>

#include "tracking/tracker.hpp"

namespace vsort::service {

// P40.40 timing thresholds. All relative to the measured interval: nothing depends on the speed.
struct TimingOptions {
    std::uint64_t maxGapFill{100}; // larger frame-ID jumps count as a camera restart
    std::uint32_t history{8};      // single-cup intervals used for the prediction
    double missFactor{1.5};        // interval >= factor x prediction: suspected missed trigger(s)
    double extraFactor{0.5};       // interval < factor x prediction: extra frame (double trigger)
    std::uint32_t maxInsert{5};    // more suspected misses than this: a stop, not misses
    double confirmTolerance{0.3};  // the next interval must match the prediction this well
    std::uint32_t stableIntervals{4}; // Starting -> Running after this many intervals that ...
    double stableTolerance{0.2};      // ... fit a straight line within this relative error
    double steadyTolerance{0.1};      // constant speed: last intervals within this of their mean
    std::chrono::milliseconds stopTimeoutMin{1000};
    double stopTimeoutPeriods{5.0}; // silence > max(min, periods x prediction): camera Silent
};

// What the timing decided about one frame. Decisions come out in frame order.
struct FrameDecision {
    camera::FrameMetadata meta;
    std::uint64_t missedBefore{0}; // NoData cups before this frame (frame-ID gaps + timing)
    std::uint64_t timingMisses{0}; // part of missedBefore that only the timing found
    bool extra{false};             // spurious frame: not a cup
    bool idReset{false};           // camera restarted its frame counter: earlier misses unknown
};

// P40.40 (a-c): miss detection for one camera from its own clock (device timestamp, host time
// if the camera has none). Intervals are never compared between cameras.
// Silent -> (first frame) Starting -> (stable intervals) Running -> (silence) Silent.
// Only Running inserts timing misses, and only after the NEXT interval confirms that the speed
// did not change (a stop and restart is then not taken for missed cups). That frame is held
// until the next frame or the stop timeout.
class CameraTiming {
public:
    explicit CameraTiming(TimingOptions options = {});

    void onFrame(const camera::FrameMetadata& meta, std::vector<FrameDecision>& out);
    void onTick(Timestamp now, std::vector<FrameDecision>& out);

    [[nodiscard]] CameraPhase phase() const noexcept { return phase_; }
    // Running at constant speed (no ramp): the cross-sensor check only compares steady cameras.
    [[nodiscard]] bool steady() const;
    // Predicted single-cup interval in ns; nullopt without history.
    [[nodiscard]] std::optional<double> intervalNs() const { return predict(1); }

private:
    struct Pending {
        FrameDecision decision;
        std::uint64_t steps{0};
        double intervalNs{0.0};
    };

    [[nodiscard]] std::optional<double> predict(std::uint64_t ahead) const;
    [[nodiscard]] bool stable() const;
    [[nodiscard]] std::chrono::nanoseconds stopTimeout() const;
    void push(double intervalNs, std::uint64_t steps);
    void restart();
    void resolvePending(bool confirmed, std::vector<FrameDecision>& out);
    void accept(const FrameDecision& decision, double own, std::vector<FrameDecision>& out);

    TimingOptions options_;
    camera::FrameIdTracker ids_;
    CameraPhase phase_{CameraPhase::Silent};
    std::deque<double> history_;
    double lastOwn_{0.0};
    Timestamp lastHost_;
    std::optional<Pending> pending_;
};

} // namespace vsort::service

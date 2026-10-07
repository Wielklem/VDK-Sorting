#include "tracking/camera_timing.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace vsort::service {

namespace {

double ownTime(const camera::FrameMetadata& meta) {
    return meta.deviceTimestampNs != 0 ? static_cast<double>(meta.deviceTimestampNs)
                                       : static_cast<double>(meta.hostTimestamp.ns());
}

// Least-squares line through (i, values[first + i]); returns {value at i = 0, slope}.
std::pair<double, double> fitLine(const std::deque<double>& values, std::size_t first) {
    const auto n = static_cast<double>(values.size() - first);
    double sx = 0.0;
    double sy = 0.0;
    double sxx = 0.0;
    double sxy = 0.0;
    for (std::size_t i = first; i < values.size(); ++i) {
        const auto x = static_cast<double>(i - first);
        sx += x;
        sy += values[i];
        sxx += x * x;
        sxy += x * values[i];
    }
    const double den = (n * sxx) - (sx * sx);
    const double slope = den != 0.0 ? ((n * sxy) - (sx * sy)) / den : 0.0;
    return {(sy - (slope * sx)) / n, slope};
}

} // namespace

CameraTiming::CameraTiming(TimingOptions options)
    : options_{options} {}

std::optional<double> CameraTiming::predict(std::uint64_t ahead) const {
    if (history_.empty()) {
        return std::nullopt;
    }
    const double last = history_.back();
    if (history_.size() < 3) {
        return last;
    }
    const auto [start, slope] = fitLine(history_, 0);
    const double x = static_cast<double>(history_.size() - 1) + static_cast<double>(ahead);
    return std::clamp(start + (slope * x), 0.5 * last, 2.0 * last);
}

bool CameraTiming::stable() const {
    const std::size_t m = std::max<std::size_t>(options_.stableIntervals, 2);
    if (history_.size() < m) {
        return false;
    }
    const std::size_t first = history_.size() - m;
    const auto [start, slope] = fitLine(history_, first);
    for (std::size_t i = first; i < history_.size(); ++i) {
        const double fit = start + (slope * static_cast<double>(i - first));
        if (fit <= 0.0 || std::abs(history_[i] - fit) > options_.stableTolerance * fit) {
            return false;
        }
    }
    return true;
}

bool CameraTiming::steady() const {
    const std::size_t m = std::max<std::size_t>(options_.stableIntervals, 2);
    if (phase_ != CameraPhase::Running || pending_ || history_.size() < m) {
        return false;
    }
    double mean = 0.0;
    for (std::size_t i = history_.size() - m; i < history_.size(); ++i) {
        mean += history_[i];
    }
    mean /= static_cast<double>(m);
    for (std::size_t i = history_.size() - m; i < history_.size(); ++i) {
        if (std::abs(history_[i] - mean) > options_.steadyTolerance * mean) {
            return false;
        }
    }
    return true;
}

std::chrono::nanoseconds CameraTiming::stopTimeout() const {
    const auto minimum = std::chrono::nanoseconds{options_.stopTimeoutMin};
    const auto p = predict(1);
    if (!p) {
        return minimum;
    }
    const auto byPeriods =
        std::chrono::nanoseconds{static_cast<std::int64_t>(options_.stopTimeoutPeriods * *p)};
    return std::max(minimum, byPeriods);
}

void CameraTiming::push(double intervalNs, std::uint64_t steps) {
    const std::size_t capacity =
        std::max<std::size_t>({options_.history, options_.stableIntervals, 3});
    for (std::uint64_t i = 0; i < steps; ++i) {
        history_.push_back(intervalNs);
        while (history_.size() > capacity) {
            history_.pop_front();
        }
    }
}

void CameraTiming::restart() {
    history_.clear();
    phase_ = CameraPhase::Starting;
}

void CameraTiming::resolvePending(bool confirmed, std::vector<FrameDecision>& out) {
    auto pending = std::move(*pending_);
    pending_.reset();
    if (confirmed) {
        const std::uint64_t known = pending.decision.missedBefore;
        pending.decision.missedBefore = pending.steps - 1U;
        pending.decision.timingMisses = pending.decision.missedBefore - known;
        push(pending.intervalNs / static_cast<double>(pending.steps), pending.steps);
    } else {
        restart(); // a pause or a stop: the speed after it is unknown
    }
    out.push_back(pending.decision);
}

void CameraTiming::accept(const FrameDecision& decision, double own,
                          std::vector<FrameDecision>& out) {
    out.push_back(decision);
    lastOwn_ = own;
    lastHost_ = decision.meta.hostTimestamp;
}

void CameraTiming::onFrame(const camera::FrameMetadata& meta, std::vector<FrameDecision>& out) {
    const auto seen = ids_.observe(meta.frameId);
    bool reset = seen.kind == camera::FrameIdTracker::Kind::Reset;
    std::uint64_t gap = 0;
    if (seen.kind == camera::FrameIdTracker::Kind::Gap) {
        if (seen.missed <= options_.maxGapFill) {
            gap = seen.missed;
        } else {
            reset = true;
        }
    }
    const double own = ownTime(meta);
    const double delta = own - lastOwn_;
    FrameDecision decision{
        .meta = meta, .missedBefore = gap, .timingMisses = 0, .extra = false, .idReset = reset};

    if (pending_) {
        bool confirmed = false;
        if (!reset && delta > 0.0) {
            const auto expected = predict(pending_->steps + 1U);
            const double ratio = delta / (static_cast<double>(gap + 1U) * expected.value_or(1.0));
            confirmed = expected && std::abs(ratio - 1.0) < options_.confirmTolerance;
        }
        resolvePending(confirmed, out);
    }

    if (phase_ == CameraPhase::Silent || reset || delta <= 0.0) {
        // First frame, first frame after a stop, camera restart or clock reset: no interval.
        if (reset) {
            decision.missedBefore = 0;
        }
        restart();
        accept(decision, own, out);
        return;
    }

    const std::uint64_t steps = gap + 1U;
    if (phase_ == CameraPhase::Running) {
        const double expected = predict(1).value_or(delta);
        const double ratio = delta / expected;
        if (gap == 0 && ratio < options_.extraFactor) {
            decision.extra = true;
            out.push_back(decision); // not a cup; the next interval is measured from the last cup
            return;
        }
        if (ratio >= options_.missFactor) {
            const auto k =
                std::max<std::uint64_t>(static_cast<std::uint64_t>(std::llround(ratio)), steps);
            if (k - 1U > options_.maxInsert) {
                restart(); // too long for misses: the machine paused or stopped
                accept(decision, own, out);
                return;
            }
            if (k > steps) {
                pending_ = Pending{.decision = decision, .steps = k, .intervalNs = delta};
                lastOwn_ = own;
                lastHost_ = meta.hostTimestamp;
                return;
            }
        }
        push(delta / static_cast<double>(steps), steps);
        accept(decision, own, out);
        return;
    }

    // Starting: count frames, learn the interval, no timing-based insertion.
    push(delta / static_cast<double>(steps), steps);
    if (stable()) {
        phase_ = CameraPhase::Running;
    }
    accept(decision, own, out);
}

void CameraTiming::onTick(Timestamp now, std::vector<FrameDecision>& out) {
    if (phase_ == CameraPhase::Silent) {
        return;
    }
    const auto timeout = stopTimeout();
    if (pending_ && now - pending_->decision.meta.hostTimestamp > timeout) {
        resolvePending(false, out);
    }
    if (now - lastHost_ > timeout) {
        history_.clear();
        phase_ = CameraPhase::Silent;
    }
}

} // namespace vsort::service

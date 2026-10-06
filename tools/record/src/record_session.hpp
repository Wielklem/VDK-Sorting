#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/camera/health.hpp>
#include <vsort/camera/resilient_camera.hpp>
#include <vsort/common/error.hpp>
#include <vsort/recorder/recorder.hpp>

#include "record_cli.hpp"

namespace vsort::record {

struct RecordPlan {
    std::filesystem::path outDir; // required
    std::string label;
    std::vector<CameraSpec> cameras;
    camera::CameraSettings settings;
    std::chrono::milliseconds duration{0}; // 0 = no time limit
    std::uint64_t framesPerCamera{0};      // 0 = no frame limit
    std::size_t queueDepth{32};
};

struct CameraResult {
    std::uint16_t index{0};
    std::string serial;
    camera::CameraHealth health;
};

struct RecordReport {
    std::filesystem::path sessionDir;
    recorder::RecorderStats stats;
    std::vector<CameraResult> cameras;
    std::chrono::milliseconds elapsed{0};

    // true when every camera delivered frames and nothing was dropped, failed or missing.
    [[nodiscard]] bool clean() const noexcept;
};

// Returns true when recording should stop. May be empty (only the limits in the plan apply).
using StopCheck = std::function<bool()>;

// Frame buffers per camera for the adapter: the writer queue (rounded up to a power of two)
// plus headroom for the frames in flight, so the recorder queue never starves the pool.
[[nodiscard]] std::size_t framePoolSize(std::size_t queueDepth) noexcept;

[[nodiscard]] RecordPlan makePlan(const Options& options);

// Opens all cameras and applies the settings first (so a missing camera creates no session),
// then records until stopRequested() returns true, the duration passes, or every camera has
// delivered framesPerCamera frames. Prints one progress line per second to `out`.
// The caller owns the camera type: `factory` makes a closed camera (Daheng, or a fake in tests).
[[nodiscard]] Result<RecordReport> runRecording(const RecordPlan& plan,
                                                const camera::CameraFactory& factory,
                                                const StopCheck& stopRequested, std::ostream& out);

} // namespace vsort::record

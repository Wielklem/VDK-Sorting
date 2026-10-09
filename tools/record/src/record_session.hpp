#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/camera/health.hpp>
#include <vsort/camera/resilient_camera.hpp>
#include <vsort/common/error.hpp>
#include <vsort/recorder/recorder.hpp>

#include "record_cli.hpp"

namespace vsort::record {

// Saved settings per logical camera ID (config module camera_settings, P30.85).
using SavedSettings = std::map<std::uint16_t, camera::CameraSettings>;

struct RecordPlan {
    std::filesystem::path outDir; // required
    std::string label;
    std::vector<CameraSpec> cameras;       // each with the settings to apply
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

// P20.95: loads camera_settings from the config folder of a service root (the layout of
// vsort_service --root); saved settings are never changed. NotFound: the root has no config
// folder (wrong --root).
// Shortcut: when camera_settings does not exist yet, the config store creates it with its
// defaults (no cameras), as the service does at its first start.
[[nodiscard]] Result<SavedSettings> loadSavedSettings(const std::filesystem::path& root);

// The settings per camera: saved (when `saved` is given; a camera without a saved entry is an
// error) or the CameraSettings defaults, then the overrides of the options. InvalidArgument: no
// saved settings for a camera, or a software trigger (nothing sends software triggers here).
[[nodiscard]] Result<RecordPlan> makePlan(const Options& options,
                                          const SavedSettings* saved = nullptr);

// One line for the label and the console, e.g. "exposure 750 us, gain 10 dB, trigger hardware
// (rising), roi full".
[[nodiscard]] std::string describeSettings(const camera::CameraSettings& settings);

// Opens all cameras and applies their settings first (so a missing camera creates no session),
// then records until stopRequested() returns true, the duration passes, or every camera has
// delivered framesPerCamera frames. Prints one progress line per second to `out`.
// The caller owns the camera type: `factory` makes a closed camera (Daheng, or a fake in tests).
[[nodiscard]] Result<RecordReport> runRecording(const RecordPlan& plan,
                                                const camera::CameraFactory& factory,
                                                const StopCheck& stopRequested, std::ostream& out);

} // namespace vsort::record

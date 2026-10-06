#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

#include <vsort/camera/camera.hpp>
#include <vsort/common/error.hpp>

namespace vsort::replay {

struct ReplayConfig {
    std::filesystem::path sessionDir; // folder written by the Recorder (M30.30)
    double speed{1.0};                // 1.0 = original rate, 2.0 = twice as fast; finite and > 0
    bool loop{false};                 // false: stop at the end; true: start again, forever
};

struct ReplayStats {
    std::uint64_t framesDelivered{0};
    std::uint64_t framesFailed{0}; // payload could not be read (reported as FrameDropped)
    std::uint64_t loops{0};        // completed passes through the recording
    bool finished{false};          // end reached (never true when looping)
};

// Replay camera (M30.20, P20.70): plays one camera of a recorded session through ICamera.
//
// - open(serial) picks the camera with that serial from <sessionDir>/session.json and indexes its
//   camNN.vrec file. A file that ends in a half-written frame is played up to that frame.
// - Frames are delivered at the recorded spacing (hostTimestamp differences) divided by `speed`.
//   Late frames are delivered at once, without drift. Use a very large speed for "as fast as
//   possible".
// - Frame IDs, sizes and pixel data are the recorded ones, so frame-ID gaps in the recording
//   are replayed too. Host timestamps are fresh (arrival now), as with a live camera.
// - With loop, frame IDs keep increasing across passes and the seam between passes is one
//   average frame interval, so the output looks like one continuous stream.
// - applySettings() is accepted but has no effect on the pixels (they are already recorded).
//   Exposure and gain are validated and stored. A ROI is NotSupported.
// - start() always begins at the first frame. isStreaming() stays true until stop(), also after
//   the end of a non-looping recording; use waitUntilFinished() or stats() for the end.
// - setFrameCallback(), setEventCallback() and the lifecycle methods are called from one
//   control thread. setSpeed(), speed(), stats() and waitUntilFinished() work from any thread.
class ReplayCamera final : public camera::ICamera {
public:
    explicit ReplayCamera(ReplayConfig config);
    ~ReplayCamera() override;

    // InvalidArgument: empty serial, empty sessionDir, bad speed. AlreadyExists: already open.
    // NotFound: no session.json, serial not in the session, or no frames.
    // ParseError: damaged session.json or recording. NotSupported: unknown file version.
    // IoError: file cannot be opened.
    [[nodiscard]] Result<> open(std::string_view serial) override;
    void close() noexcept override;
    [[nodiscard]] bool isOpen() const noexcept override;

    [[nodiscard]] Result<camera::CameraInfo> info() const override;
    [[nodiscard]] Result<camera::CameraSettings> settings() const override;
    [[nodiscard]] Result<> applySettings(const camera::CameraSettings& settings) override;

    void setFrameCallback(camera::FrameCallback callback) override;
    // Only FrameDropped is reported (a payload that cannot be read). Never Offline.
    void setEventCallback(camera::CameraEventCallback callback) override;

    // AlreadyExists: already streaming. NotFound: not open.
    [[nodiscard]] Result<> start() override;
    void stop() noexcept override;
    [[nodiscard]] bool isStreaming() const noexcept override;

    // Changes the rate now, also while streaming. InvalidArgument unless finite and > 0.
    [[nodiscard]] Result<> setSpeed(double speed);
    [[nodiscard]] double speed() const noexcept;

    // Frames in the opened recording, 0 when not open.
    [[nodiscard]] std::uint64_t frameCount() const noexcept;

    [[nodiscard]] ReplayStats stats() const noexcept;

    // True once a non-looping recording has been played to the end (or immediately if it has
    // been already). False on timeout, and always false when looping.
    [[nodiscard]] bool waitUntilFinished(std::chrono::milliseconds timeout) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vsort::replay

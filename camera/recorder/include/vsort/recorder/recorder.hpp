#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/common/error.hpp>

namespace vsort::recorder {

// One camera that takes part in a session.
struct CameraStream {
    std::uint16_t cameraIndex{0}; // logical camera ID, unique within the session
    std::string serial;
    std::string model;
};

struct RecorderConfig {
    std::filesystem::path rootDir; // sessions are created below this folder
    std::string label;             // free text, stored in session.json
    // Max frames waiting for the writer (rounded up to a power of two). Each queued frame holds
    // its pool buffer, so keep this below the camera's FramePool size.
    std::size_t queueDepth{32};
};

struct RecorderStats {
    std::uint64_t written{0}; // frames on disk
    std::uint64_t dropped{0}; // queue full, or submitted after finish()
    std::uint64_t failed{0};  // unknown camera index or write error
};

// Raw frame recorder (M30.30, P20.60).
//
// One folder per session: <rootDir>/<YYYYMMDDTHHMMSS>/ with session.json and one camNN.vrec per
// camera (format: recording_format.hpp). session.json has status "recording" while the session
// runs and "complete" after finish(), so an interrupted session is recognisable.
//
// submit() is cheap and never blocks, so it is safe on a camera grab thread. A writer thread does
// the disk I/O. Stop the cameras before finish() or destroying the recorder.
class Recorder {
public:
    // InvalidArgument: empty rootDir, no cameras, duplicate camera index, queueDepth 0.
    // IoError: folder or file cannot be created.
    [[nodiscard]] static Result<std::unique_ptr<Recorder>> start(const RecorderConfig& config,
                                                                 std::vector<CameraStream> cameras);

    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    Recorder(Recorder&&) = delete;
    Recorder& operator=(Recorder&&) = delete;

    // Uses frame.meta.cameraIndex. Keeps frame.owner alive until the frame is written.
    void submit(const camera::Frame& frame) noexcept;

    // Frame callback for one camera. Sets meta.cameraIndex, then submits. The recorder must
    // outlive the camera's streaming.
    [[nodiscard]] camera::FrameCallback callbackFor(std::uint16_t cameraIndex);

    // Writes the remaining frames and the final session.json. Safe to call twice.
    [[nodiscard]] Result<> finish();

    [[nodiscard]] const std::filesystem::path& sessionDir() const noexcept;
    [[nodiscard]] RecorderStats stats() const noexcept;

private:
    struct Impl;
    explicit Recorder(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace vsort::recorder

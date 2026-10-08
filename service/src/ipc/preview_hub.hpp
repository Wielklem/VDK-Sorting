#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/common/error.hpp>
#include <vsort/common/timestamp.hpp>
#include <vsort/ipc/preview_ring.hpp>

namespace vsort::service {

inline constexpr std::uint16_t kMinPreviewFps = 1;
inline constexpr std::uint16_t kMaxPreviewFps = 60;
inline constexpr std::uint32_t kMinPreviewWidth = 16;
inline constexpr std::uint32_t kMaxPreviewWidth = 4096;

struct PreviewSettings {
    bool enabled{false};
    std::uint16_t fps{15};
    std::uint32_t maxWidth{640};
};

// P30.86: 15 fps, so a 10 Hz camera is not thinned out by the fps limit (that limit only shapes
// the live picture; the frame rates shown in the HMI come from CameraRates).
struct PreviewDefaults {
    std::uint16_t fps{15};
    std::uint32_t maxWidth{640};
    std::uint16_t slotCount{ipc::kDefaultSlotCount};
};

// What a reader needs to attach. shmName empty = no ring (not started yet, or closed).
struct PreviewStream {
    std::uint16_t cameraId{0};
    std::string shmName;
    std::uint32_t generation{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    ipc::PreviewPixelFormat format{ipc::PreviewPixelFormat::Mono8};
};

struct PreviewCounters {
    std::uint64_t offered{0};   // frames seen while the preview was enabled
    std::uint64_t skipped{0};   // dropped by the fps limit
    std::uint64_t replaced{0};  // overwritten in the mailbox before the worker got to them
    std::uint64_t published{0}; // written to a ring
    std::uint64_t errors{0};    // downscale or ring failures
};

// Takes frames from the cameras, downscales them at the configured fps and width on its own
// thread and writes them to one shared-memory ring per camera (P30.20).
//
// submit() is called from camera grab threads: it only checks the fps limit and parks the frame
// (shared ownership, no pixel copy when Frame::owner is set) in a one-frame mailbox.
class PreviewHub {
public:
    using StreamCallback = std::function<void(const PreviewStream&)>;

    // The callback runs on the hub's worker thread, when a ring was created or closed.
    explicit PreviewHub(PreviewDefaults defaults, StreamCallback onStreamChanged = {});
    ~PreviewHub();
    PreviewHub(const PreviewHub&) = delete;
    PreviewHub& operator=(const PreviewHub&) = delete;
    PreviewHub(PreviewHub&&) = delete;
    PreviewHub& operator=(PreviewHub&&) = delete;

    [[nodiscard]] Result<> start();
    void stop() noexcept; // closes all rings

    void submit(const camera::Frame& frame) noexcept;

    // InvalidArgument when fps or maxWidth are outside the limits above.
    // Disabling closes the ring (readers get an event, the old ring lives another second).
    [[nodiscard]] Result<PreviewSettings> setPreview(std::uint16_t cameraId,
                                                     const PreviewSettings& settings);

    [[nodiscard]] PreviewSettings settings(std::uint16_t cameraId) const;
    [[nodiscard]] PreviewStream stream(std::uint16_t cameraId) const;
    [[nodiscard]] PreviewCounters counters() const;

private:
    struct CameraState {
        PreviewSettings settings;
        bool hasLast{false};
        Timestamp lastAccepted;
        std::optional<camera::Frame> pending;
        bool closeRequested{false};
        PreviewStream stream;
        // Worker thread only:
        std::unique_ptr<ipc::PreviewRingWriter> ring;
        std::uint32_t generation{0};
    };
    struct Job {
        std::uint16_t cameraId{0};
        CameraState* state{nullptr};
        camera::Frame frame;
        PreviewSettings settings;
    };
    struct Retired {
        std::unique_ptr<ipc::PreviewRingWriter> ring;
        Timestamp closeAt;
    };

    void run();
    void process(const Job& job);
    void closeRing(std::uint16_t cameraId, CameraState& state);
    void retire(std::unique_ptr<ipc::PreviewRingWriter> ring);
    void reapRetired(bool all);
    void noteError(std::uint16_t cameraId, const Error& error);
    [[nodiscard]] CameraState& stateLocked(std::uint16_t cameraId);

    PreviewDefaults defaults_;
    StreamCallback onStreamChanged_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::map<std::uint16_t, CameraState> cameras_; // nodes are never erased
    bool hasWork_{false};
    bool stopping_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;

    std::vector<Retired> retired_; // worker thread only

    std::atomic<std::uint64_t> offered_{0};
    std::atomic<std::uint64_t> skipped_{0};
    std::atomic<std::uint64_t> replaced_{0};
    std::atomic<std::uint64_t> published_{0};
    std::atomic<std::uint64_t> errors_{0};
};

} // namespace vsort::service

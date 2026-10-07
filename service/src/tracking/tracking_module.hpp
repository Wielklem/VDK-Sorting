#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/common/bounded_queue.hpp>
#include <vsort/common/config_store.hpp>
#include <vsort/common/message_bus.hpp>
#include <vsort/common/module.hpp>

#include "tracking/tracker.hpp"

namespace vsort::service {

// Bounded hand-over from the camera grab threads to the tracking thread. Carries metadata only
// (no pixels), so it never holds camera buffers. Closed: frames are ignored, not counted.
class FrameInbox {
public:
    explicit FrameInbox(std::size_t capacity)
        : queue_{capacity} {}

    void open() noexcept { open_.store(true, std::memory_order_release); }
    void close() noexcept { open_.store(false, std::memory_order_release); }

    // Grab thread. Never blocks. False: closed, or full (counted as dropped).
    bool push(const camera::FrameMetadata& meta) noexcept;
    [[nodiscard]] std::optional<camera::FrameMetadata> pop() { return queue_.tryPop(); }

    [[nodiscard]] std::uint64_t accepted() const noexcept {
        return accepted_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::size_t capacity() const noexcept { return queue_.capacity(); }

private:
    BoundedQueue<camera::FrameMetadata> queue_;
    std::atomic<bool> open_{false};
    std::atomic<std::uint64_t> accepted_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

struct TrackingOptions {
    std::size_t queueCapacity{256};
    std::chrono::milliseconds tickInterval{100};
    std::chrono::seconds logInterval{10};
};

// IModule "tracking" (M50, P40.30). Frames come in through submit() (a camera frame sink); the
// tracker runs on the module's own thread and publishes ObjectRecords (MSG-50-01) on the bus.
// A frame dropped in the inbox shows up as a frame-ID gap, so the tracker marks it NoData.
class TrackingModule final : public IModule {
public:
    // Builds a FrameSequenceTracker from the "machine" and "rois" config in init().
    // `config` must outlive the module.
    explicit TrackingModule(const IConfigStore* config, TrackingOptions options = {});
    // Uses the given tracker (tests, later trackers).
    explicit TrackingModule(std::unique_ptr<ITracker> tracker, TrackingOptions options = {});
    ~TrackingModule() override;
    TrackingModule(const TrackingModule&) = delete;
    TrackingModule& operator=(const TrackingModule&) = delete;
    TrackingModule(TrackingModule&&) = delete;
    TrackingModule& operator=(TrackingModule&&) = delete;

    [[nodiscard]] std::string_view name() const noexcept override { return "tracking"; }
    [[nodiscard]] std::vector<std::string> dependencies() const override { return {"camera"}; }
    [[nodiscard]] Result<> init(ModuleContext& context) override;
    [[nodiscard]] Result<> start() override;
    void stop() noexcept override;
    [[nodiscard]] Health health() const override;

    // Frame sink: called on a camera grab thread, fast, never blocks. Ignored unless running.
    void submit(const camera::Frame& frame) noexcept { inbox_.push(frame.meta); }

    [[nodiscard]] std::uint64_t framesDropped() const noexcept { return inbox_.dropped(); }
    [[nodiscard]] std::uint64_t framesProcessed() const noexcept {
        return processed_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t recordsPublished() const noexcept {
        return published_.load(std::memory_order_relaxed);
    }

private:
    void run(const std::stop_token& stop);
    void publish(std::vector<ObjectRecord>& records);
    void logCounters();

    const IConfigStore* config_{nullptr};
    TrackingOptions options_;
    std::unique_ptr<ITracker> tracker_;
    MessageBus* bus_{nullptr};
    FrameInbox inbox_;
    std::jthread thread_;
    std::atomic<std::uint64_t> processed_{0};
    std::atomic<std::uint64_t> published_{0};
    std::atomic<std::int64_t> lastDropNs_{0};
    std::uint64_t droppedSeen_{0};           // tracking thread only
    std::vector<SensorCounters> lastLogged_; // tracking thread only
};

} // namespace vsort::service

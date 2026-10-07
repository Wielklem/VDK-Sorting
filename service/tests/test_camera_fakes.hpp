#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/camera/camera_map.hpp>
#include <vsort/common/timestamp.hpp>

#include "camera/camera_backend.hpp"

namespace vsort::testutil {

// Polls `pred` until it is true or the timeout passes.
template <typename Pred>
bool waitFor(Pred&& pred, std::chrono::milliseconds timeout = std::chrono::milliseconds{5000}) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return pred();
}

// What the fake cameras "see": which serial numbers are plugged in, and what was applied last.
class FakeWorld {
public:
    void plug(const std::string& serial) {
        const std::scoped_lock lock{mutex_};
        plugged_.insert(serial);
    }
    void unplug(const std::string& serial) {
        const std::scoped_lock lock{mutex_};
        plugged_.erase(serial);
    }
    [[nodiscard]] bool isPlugged(const std::string& serial) const {
        const std::scoped_lock lock{mutex_};
        return plugged_.contains(serial);
    }
    [[nodiscard]] std::vector<std::string> plugged() const {
        const std::scoped_lock lock{mutex_};
        return {plugged_.begin(), plugged_.end()};
    }
    void recordApplied(const std::string& serial, const camera::CameraSettings& settings) {
        const std::scoped_lock lock{mutex_};
        applied_[serial] = settings;
    }
    [[nodiscard]] std::optional<camera::CameraSettings> applied(const std::string& serial) const {
        const std::scoped_lock lock{mutex_};
        const auto it = applied_.find(serial);
        if (it == applied_.end()) {
            return std::nullopt;
        }
        return it->second;
    }
    void clearApplied() {
        const std::scoped_lock lock{mutex_};
        applied_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::set<std::string> plugged_;
    std::map<std::string, camera::CameraSettings> applied_;
};

// Streams 64x48 Mono8 frames at about 200 fps once started.
class FakeCamera final : public camera::ICamera {
public:
    static constexpr std::uint32_t kWidth = 64;
    static constexpr std::uint32_t kHeight = 48;

    explicit FakeCamera(FakeWorld& world)
        : world_{world}
        , pixels_{std::make_shared<std::vector<std::byte>>(
              static_cast<std::size_t>(kWidth) * kHeight, std::byte{0x55})} {}
    ~FakeCamera() override { close(); }

    [[nodiscard]] Result<> open(std::string_view serial) override {
        if (!world_.isPlugged(std::string{serial})) {
            return makeError(Errc::DeviceError, "camera not plugged in");
        }
        serial_ = std::string{serial};
        open_ = true;
        return {};
    }
    void close() noexcept override {
        stop();
        open_ = false;
    }
    [[nodiscard]] bool isOpen() const noexcept override { return open_; }
    [[nodiscard]] Result<camera::CameraInfo> info() const override {
        camera::CameraInfo info;
        info.serial = serial_;
        info.model = "FAKE";
        info.sensorWidth = kWidth;
        info.sensorHeight = kHeight;
        return info;
    }
    [[nodiscard]] Result<camera::CameraSettings> settings() const override {
        const std::scoped_lock lock{mutex_};
        return settings_;
    }
    [[nodiscard]] Result<> applySettings(const camera::CameraSettings& settings) override {
        if (settings.exposureUs <= 0.0) {
            return makeError(Errc::InvalidArgument, "exposure must be > 0");
        }
        {
            const std::scoped_lock lock{mutex_};
            settings_ = settings;
        }
        world_.recordApplied(serial_, settings);
        return {};
    }
    void setFrameCallback(camera::FrameCallback callback) override {
        callback_ = std::move(callback);
    }
    [[nodiscard]] Result<> start() override {
        if (!open_) {
            return makeError(Errc::NotFound, "not open");
        }
        if (streaming_) {
            return makeError(Errc::AlreadyExists, "already streaming");
        }
        streaming_ = true;
        thread_ = std::jthread{[this](const std::stop_token& token) { produce(token); }};
        return {};
    }
    void stop() noexcept override {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
        streaming_ = false;
    }
    [[nodiscard]] bool isStreaming() const noexcept override { return streaming_; }

private:
    void produce(const std::stop_token& stop) {
        std::uint64_t id = 0;
        while (!stop.stop_requested()) {
            camera::Frame frame;
            frame.meta.frameId = FrameId{++id};
            frame.meta.hostTimestamp = Timestamp::now();
            frame.meta.width = kWidth;
            frame.meta.height = kHeight;
            frame.meta.strideBytes = kWidth;
            frame.meta.pixelFormat = camera::PixelFormat::Mono8;
            frame.data = std::span<const std::byte>{*pixels_};
            frame.owner = pixels_;
            if (callback_) {
                callback_(frame);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }

    FakeWorld& world_;
    std::shared_ptr<std::vector<std::byte>> pixels_;
    std::string serial_;
    mutable std::mutex mutex_;
    camera::CameraSettings settings_;
    camera::FrameCallback callback_;
    std::atomic<bool> open_{false};
    std::atomic<bool> streaming_{false};
    std::jthread thread_;
};

class FakeBackend final : public service::ICameraBackend {
public:
    explicit FakeBackend(FakeWorld& world, bool persistent = true,
                         std::vector<camera::CameraMapEntry> fixed = {})
        : world_{world}
        , persistent_{persistent}
        , fixed_{std::move(fixed)} {}

    [[nodiscard]] std::string_view name() const noexcept override { return "fake"; }
    [[nodiscard]] Result<std::vector<camera::DiscoveredCamera>> discover() override {
        std::vector<camera::DiscoveredCamera> out;
        for (const auto& serial : world_.plugged()) {
            camera::DiscoveredCamera found;
            found.serial = serial;
            found.model = "FAKE";
            out.push_back(std::move(found));
        }
        return out;
    }
    [[nodiscard]] std::unique_ptr<camera::ICamera> create() override {
        return std::make_unique<FakeCamera>(world_);
    }
    [[nodiscard]] bool persistent() const noexcept override { return persistent_; }
    [[nodiscard]] std::vector<camera::CameraMapEntry> fixedMap() const override { return fixed_; }

private:
    FakeWorld& world_;
    bool persistent_;
    std::vector<camera::CameraMapEntry> fixed_;
};

} // namespace vsort::testutil

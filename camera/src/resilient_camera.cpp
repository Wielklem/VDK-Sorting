#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <vsort/camera/resilient_camera.hpp>
#include <vsort/common/logging.hpp>

namespace vsort::camera {

namespace {

constexpr auto kPollInterval = std::chrono::milliseconds{100};

void warn(const std::string& message) noexcept {
    try {
        log::get("camera")->warn("{}", message);
    } catch (...) {
        // Logging must never take a camera thread down.
    }
}

void note(const std::string& message) noexcept {
    try {
        log::get("camera")->info("{}", message);
    } catch (...) {
        // Logging must never take a camera thread down.
    }
}

class ResilientCamera final : public IMonitoredCamera {
public:
    ResilientCamera(CameraFactory factory, const ResilientOptions& options)
        : factory_{std::move(factory)}
        , backoff_{options.retryInitial, options.retryMax} {}

    ~ResilientCamera() override { close(); }

    ResilientCamera(const ResilientCamera&) = delete;
    ResilientCamera& operator=(const ResilientCamera&) = delete;
    ResilientCamera(ResilientCamera&&) = delete;
    ResilientCamera& operator=(ResilientCamera&&) = delete;

    [[nodiscard]] Result<> open(std::string_view serial) override {
        if (serial.empty()) {
            return makeError(Errc::InvalidArgument, "empty serial");
        }
        const std::lock_guard lock{mutex_};
        if (state_ != CameraState::Closed) {
            return makeError(Errc::AlreadyExists, "camera already open");
        }
        auto inner = makeInner();
        if (!inner) {
            return makeError(Errc::Internal, "camera factory returned no camera");
        }
        if (auto r = inner->open(serial); !r) {
            return r;
        }
        serial_ = std::string{serial};
        inner_ = std::move(inner);
        hasSettings_ = false;
        wantStreaming_ = false;
        offline_ = false;
        stopRequested_ = false;
        resetCounters();
        state_ = CameraState::Open;
        supervisor_ = std::thread{[this] { supervise(); }};
        return {};
    }

    void close() noexcept override {
        std::thread worker;
        {
            const std::lock_guard lock{mutex_};
            stopRequested_ = true;
            worker = std::move(supervisor_);
        }
        cv_.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
        const std::lock_guard lock{mutex_};
        if (inner_) {
            inner_->close();
            inner_.reset();
        }
        wantStreaming_ = false;
        state_ = CameraState::Closed;
    }

    [[nodiscard]] bool isOpen() const noexcept override {
        return state_.load() != CameraState::Closed;
    }

    [[nodiscard]] Result<CameraInfo> info() const override {
        const std::lock_guard lock{mutex_};
        if (auto r = usable(); !r) {
            return std::unexpected<Error>{r.error()};
        }
        return inner_->info();
    }

    [[nodiscard]] Result<CameraSettings> settings() const override {
        const std::lock_guard lock{mutex_};
        if (auto r = usable(); !r) {
            return std::unexpected<Error>{r.error()};
        }
        return inner_->settings();
    }

    [[nodiscard]] Result<> applySettings(const CameraSettings& wanted) override {
        const std::lock_guard lock{mutex_};
        if (auto r = usable(); !r) {
            return r;
        }
        if (auto r = inner_->applySettings(wanted); !r) {
            return r;
        }
        settings_ = wanted;
        hasSettings_ = true;
        return {};
    }

    // Not safe while streaming (the grab thread reads it).
    void setFrameCallback(FrameCallback callback) override { userCallback_ = std::move(callback); }

    [[nodiscard]] Result<> start() override {
        const std::lock_guard lock{mutex_};
        if (state_ == CameraState::Closed) {
            return makeError(Errc::NotFound, "not open");
        }
        if (state_ == CameraState::Reconnecting) {
            wantStreaming_ = true; // started by the reconnect
            return {};
        }
        if (state_ == CameraState::Streaming) {
            return {};
        }
        tracker_.reset();
        if (auto r = inner_->start(); !r) {
            return r;
        }
        wantStreaming_ = true;
        state_ = CameraState::Streaming;
        return {};
    }

    void stop() noexcept override {
        const std::lock_guard lock{mutex_};
        wantStreaming_ = false;
        if (inner_) {
            inner_->stop();
        }
        if (state_ == CameraState::Streaming) {
            state_ = CameraState::Open;
        }
    }

    [[nodiscard]] bool isStreaming() const noexcept override {
        return state_.load() == CameraState::Streaming;
    }

    [[nodiscard]] CameraHealth health() const override {
        CameraHealth h;
        h.state = state_.load();
        h.framesReceived = framesReceived_.load();
        h.framesDropped = framesDropped_.load();
        h.idGaps = idGaps_.load();
        h.framesMissed = framesMissed_.load();
        h.idResets = idResets_.load();
        h.reconnectAttempts = reconnectAttempts_.load();
        h.reconnects = reconnects_.load();
        const std::lock_guard lock{errorMutex_};
        h.lastError = lastError_;
        return h;
    }

private:
    // Caller holds mutex_.
    [[nodiscard]] Result<> usable() const {
        if (state_ == CameraState::Closed) {
            return makeError(Errc::NotFound, "not open");
        }
        if (state_ == CameraState::Reconnecting || !inner_) {
            return makeError(Errc::DeviceError, "camera reconnecting");
        }
        return {};
    }

    [[nodiscard]] std::unique_ptr<ICamera> makeInner() {
        auto camera = factory_();
        if (camera) {
            camera->setFrameCallback([this](const Frame& frame) { onFrame(frame); });
            camera->setEventCallback([this](const CameraEvent& event) { onEvent(event); });
        }
        return camera;
    }

    // Runs on the camera's grab thread.
    void onFrame(const Frame& frame) {
        framesReceived_.fetch_add(1);
        const auto seen = tracker_.observe(frame.meta.frameId);
        if (seen.kind == FrameIdTracker::Kind::Gap) {
            const std::uint64_t n = idGaps_.fetch_add(1) + 1;
            framesMissed_.fetch_add(seen.missed);
            if (n == 1 || n % 1000 == 0) { // rate limited
                warn("frame ID gap: " + std::to_string(seen.missed) + " frame(s) missing before " +
                     std::to_string(frame.meta.frameId.value()) + ", gaps so far " +
                     std::to_string(n));
            }
        } else if (seen.kind == FrameIdTracker::Kind::Reset) {
            idResets_.fetch_add(1);
            warn("frame ID repeated or went backwards at " +
                 std::to_string(frame.meta.frameId.value()));
        }
        if (userCallback_) {
            userCallback_(frame);
        }
    }

    // Runs on the camera's own threads. Must not take mutex_: the reconnect holds it while
    // closing the camera, which waits for running callbacks.
    void onEvent(const CameraEvent& event) noexcept {
        if (event.kind == CameraEventKind::Offline) {
            offline_ = true;
            cv_.notify_all();
        } else {
            framesDropped_.fetch_add(1);
        }
    }

    void supervise() {
        std::unique_lock lock{mutex_};
        while (!stopRequested_) {
            cv_.wait_for(lock, kPollInterval, [this] { return stopRequested_ || offline_.load(); });
            if (!stopRequested_ && offline_) {
                reconnect(lock);
            }
        }
    }

    // Holds mutex_ except while waiting between attempts. A blocked open() delays API calls.
    void reconnect(std::unique_lock<std::mutex>& lock) {
        state_ = CameraState::Reconnecting;
        setLastError("camera offline");
        warn(serial_ + ": camera offline, reconnecting");
        if (inner_) {
            inner_->close();
            inner_.reset();
        }
        backoff_.reset();
        while (!stopRequested_) {
            reconnectAttempts_.fetch_add(1);
            auto restored = restore();
            if (restored) {
                reconnects_.fetch_add(1);
                offline_ = false;
                state_ = wantStreaming_ ? CameraState::Streaming : CameraState::Open;
                note(serial_ + ": camera reconnected");
                return;
            }
            setLastError(restored.error().what());
            cv_.wait_for(lock, backoff_.next(), [this] { return stopRequested_.load(); });
        }
    }

    // Caller holds mutex_.
    [[nodiscard]] Result<> restore() {
        auto inner = makeInner();
        if (!inner) {
            return makeError(Errc::Internal, "camera factory returned no camera");
        }
        if (auto r = inner->open(serial_); !r) {
            return r;
        }
        if (hasSettings_) {
            if (auto r = inner->applySettings(settings_); !r) {
                inner->close();
                return r;
            }
        }
        tracker_.reset(); // the camera starts counting again
        if (wantStreaming_) {
            if (auto r = inner->start(); !r) {
                inner->close();
                return r;
            }
        }
        inner_ = std::move(inner);
        return {};
    }

    void setLastError(std::string message) {
        const std::lock_guard lock{errorMutex_};
        lastError_ = std::move(message);
    }

    void resetCounters() {
        framesReceived_ = 0;
        framesDropped_ = 0;
        idGaps_ = 0;
        framesMissed_ = 0;
        idResets_ = 0;
        reconnectAttempts_ = 0;
        reconnects_ = 0;
        setLastError({});
        tracker_.reset();
    }

    CameraFactory factory_;
    ReconnectBackoff backoff_;

    mutable std::mutex mutex_; // guards everything below except the atomics
    std::condition_variable cv_;
    std::thread supervisor_;
    std::unique_ptr<ICamera> inner_;
    std::string serial_;
    CameraSettings settings_;
    bool hasSettings_{false};
    bool wantStreaming_{false};
    FrameCallback userCallback_;
    FrameIdTracker tracker_; // grab thread only; reset only while no camera is streaming

    std::atomic<CameraState> state_{CameraState::Closed};
    std::atomic<bool> offline_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<std::uint64_t> framesReceived_{0};
    std::atomic<std::uint64_t> framesDropped_{0};
    std::atomic<std::uint64_t> idGaps_{0};
    std::atomic<std::uint64_t> framesMissed_{0};
    std::atomic<std::uint64_t> idResets_{0};
    std::atomic<std::uint64_t> reconnectAttempts_{0};
    std::atomic<std::uint64_t> reconnects_{0};

    mutable std::mutex errorMutex_;
    std::string lastError_;
};

} // namespace

std::unique_ptr<IMonitoredCamera> makeResilientCamera(CameraFactory factory,
                                                      const ResilientOptions& options) {
    return std::make_unique<ResilientCamera>(std::move(factory), options);
}

} // namespace vsort::camera

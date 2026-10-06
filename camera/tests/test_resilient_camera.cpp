#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include <vsort/camera/resilient_camera.hpp>

namespace {

using namespace vsort;
using namespace vsort::camera;
using namespace std::chrono_literals;

class Script;

// Camera whose behavior the test controls through a Script.
class ScriptedCamera final : public ICamera {
public:
    explicit ScriptedCamera(Script& script)
        : script_{script} {}
    ~ScriptedCamera() override { close(); }

    Result<> open(std::string_view serial) override;
    void close() noexcept override;
    [[nodiscard]] bool isOpen() const noexcept override { return open_; }
    [[nodiscard]] Result<CameraInfo> info() const override { return CameraInfo{}; }
    [[nodiscard]] Result<CameraSettings> settings() const override { return CameraSettings{}; }
    Result<> applySettings(const CameraSettings& settings) override;
    void setFrameCallback(FrameCallback callback) override { frames_ = std::move(callback); }
    void setEventCallback(CameraEventCallback callback) override { events_ = std::move(callback); }
    Result<> start() override;
    void stop() noexcept override { streaming_ = false; }
    [[nodiscard]] bool isStreaming() const noexcept override { return streaming_; }

    void emitFrame(std::uint64_t id) const {
        Frame frame;
        frame.meta.frameId = FrameId{id};
        frames_(frame);
    }
    void emitEvent(CameraEventKind kind) const { events_(CameraEvent{.kind = kind, .detail = {}}); }

private:
    Script& script_;
    FrameCallback frames_;
    CameraEventCallback events_;
    bool open_{false};
    bool streaming_{false};
};

class Script {
public:
    std::atomic<int> failOpens{0}; // next N open() calls fail
    std::atomic<int> opens{0};     // successful opens
    std::atomic<int> starts{0};
    std::atomic<double> lastExposure{0.0};

    CameraFactory factory() {
        return [this] { return std::make_unique<ScriptedCamera>(*this); };
    }

    // Run f on the currently open camera. False if there is none.
    template <typename F>
    bool withCurrent(F&& f) {
        const std::lock_guard lock{mutex_};
        if (current_ == nullptr) {
            return false;
        }
        f(*current_);
        return true;
    }

    void set(ScriptedCamera* camera) {
        const std::lock_guard lock{mutex_};
        current_ = camera;
    }
    void clear(const ScriptedCamera* camera) {
        const std::lock_guard lock{mutex_};
        if (current_ == camera) {
            current_ = nullptr;
        }
    }

private:
    std::mutex mutex_;
    ScriptedCamera* current_{nullptr};
};

Result<> ScriptedCamera::open(std::string_view /*serial*/) {
    if (script_.failOpens.load() > 0) {
        script_.failOpens.fetch_sub(1);
        return makeError(Errc::DeviceError, "scripted open failure");
    }
    open_ = true;
    script_.opens.fetch_add(1);
    script_.set(this);
    return {};
}

void ScriptedCamera::close() noexcept {
    stop();
    open_ = false;
    script_.clear(this);
}

Result<> ScriptedCamera::applySettings(const CameraSettings& settings) {
    script_.lastExposure = settings.exposureUs;
    return {};
}

Result<> ScriptedCamera::start() {
    streaming_ = true;
    script_.starts.fetch_add(1);
    return {};
}

template <typename Pred>
bool waitFor(Pred&& pred, std::chrono::milliseconds timeout = 5000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

ResilientOptions fastRetry() {
    return {.retryInitial = 1ms, .retryMax = 5ms};
}

} // namespace

TEST(ResilientCamera, OpenValidatesAndRejectsSecondOpen) {
    Script script;
    const auto camera = makeResilientCamera(script.factory(), fastRetry());
    EXPECT_EQ(camera->open("").error().code, Errc::InvalidArgument);
    EXPECT_EQ(camera->start().error().code, Errc::NotFound);
    EXPECT_FALSE(camera->isOpen());
    ASSERT_TRUE(camera->open("SN1").has_value());
    EXPECT_TRUE(camera->isOpen());
    EXPECT_EQ(camera->open("SN1").error().code, Errc::AlreadyExists);
    camera->close();
    EXPECT_FALSE(camera->isOpen());
    EXPECT_EQ(camera->health().state, CameraState::Closed);
}

TEST(ResilientCamera, OpenFailureIsReturned) {
    Script script;
    script.failOpens = 1;
    const auto camera = makeResilientCamera(script.factory(), fastRetry());
    const auto r = camera->open("SN1");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Errc::DeviceError);
    EXPECT_FALSE(camera->isOpen());
}

TEST(ResilientCamera, CountsFramesGapsAndDrops) {
    Script script;
    const auto camera = makeResilientCamera(script.factory(), fastRetry());
    std::atomic<int> delivered{0};
    camera->setFrameCallback([&](const Frame&) { delivered.fetch_add(1); });
    ASSERT_TRUE(camera->open("SN1").has_value());
    ASSERT_TRUE(camera->start().has_value());

    script.withCurrent([](ScriptedCamera& cam) {
        cam.emitFrame(1);
        cam.emitFrame(2);
        cam.emitFrame(5); // 3 and 4 missing
        cam.emitFrame(6);
        cam.emitFrame(6); // repeated
        cam.emitEvent(CameraEventKind::FrameDropped);
    });

    const CameraHealth h = camera->health();
    EXPECT_EQ(h.state, CameraState::Streaming);
    EXPECT_EQ(h.framesReceived, 5U);
    EXPECT_EQ(h.idGaps, 1U);
    EXPECT_EQ(h.framesMissed, 2U);
    EXPECT_EQ(h.idResets, 1U);
    EXPECT_EQ(h.framesDropped, 1U);
    EXPECT_EQ(delivered.load(), 5);
    camera->close();
}

TEST(ResilientCamera, ReconnectsAndRestoresSettingsAndStreaming) {
    Script script;
    const auto camera = makeResilientCamera(script.factory(), fastRetry());
    std::atomic<int> delivered{0};
    camera->setFrameCallback([&](const Frame&) { delivered.fetch_add(1); });
    ASSERT_TRUE(camera->open("SN1").has_value());
    CameraSettings wanted;
    wanted.exposureUs = 1234.0;
    ASSERT_TRUE(camera->applySettings(wanted).has_value());
    ASSERT_TRUE(camera->start().has_value());
    script.withCurrent([](ScriptedCamera& cam) { cam.emitFrame(10); });

    script.lastExposure = 0.0;
    script.withCurrent([](ScriptedCamera& cam) { cam.emitEvent(CameraEventKind::Offline); });
    ASSERT_TRUE(waitFor([&] { return camera->health().reconnects == 1; }));

    EXPECT_EQ(script.opens.load(), 2);
    EXPECT_EQ(script.starts.load(), 2);
    EXPECT_EQ(script.lastExposure.load(), 1234.0);
    EXPECT_EQ(camera->health().state, CameraState::Streaming);
    EXPECT_TRUE(camera->isStreaming());

    // The camera counts from 0 again: not a gap, and the callback still works.
    script.withCurrent([](ScriptedCamera& cam) {
        cam.emitFrame(0);
        cam.emitFrame(1);
    });
    const CameraHealth h = camera->health();
    EXPECT_EQ(h.framesReceived, 3U);
    EXPECT_EQ(h.idGaps, 0U);
    EXPECT_EQ(h.idResets, 0U);
    EXPECT_EQ(delivered.load(), 3);
    camera->close();
}

TEST(ResilientCamera, RetriesUntilOpenSucceeds) {
    Script script;
    const auto camera = makeResilientCamera(script.factory(), fastRetry());
    ASSERT_TRUE(camera->open("SN1").has_value());

    script.failOpens = 3;
    script.withCurrent([](ScriptedCamera& cam) { cam.emitEvent(CameraEventKind::Offline); });
    ASSERT_TRUE(waitFor([&] { return camera->health().reconnects == 1; }));

    const CameraHealth h = camera->health();
    EXPECT_EQ(h.reconnectAttempts, 4U);
    EXPECT_EQ(h.state, CameraState::Open);
    EXPECT_FALSE(h.lastError.empty());
    camera->close();
}

TEST(ResilientCamera, CallsAreRejectedWhileReconnectingAndStartIsRemembered) {
    Script script;
    const auto camera = makeResilientCamera(script.factory(), fastRetry());
    ASSERT_TRUE(camera->open("SN1").has_value());

    script.failOpens = 1'000'000;
    script.withCurrent([](ScriptedCamera& cam) { cam.emitEvent(CameraEventKind::Offline); });
    ASSERT_TRUE(waitFor([&] { return camera->health().state == CameraState::Reconnecting; }));

    EXPECT_TRUE(camera->isOpen());
    EXPECT_FALSE(camera->isStreaming());
    EXPECT_EQ(camera->info().error().code, Errc::DeviceError);
    EXPECT_EQ(camera->applySettings(CameraSettings{}).error().code, Errc::DeviceError);
    ASSERT_TRUE(camera->start().has_value()); // remembered

    script.failOpens = 0;
    ASSERT_TRUE(waitFor([&] { return camera->health().reconnects == 1; }));
    EXPECT_EQ(camera->health().state, CameraState::Streaming);
    camera->close();
}

TEST(ResilientCamera, CloseDuringReconnectDoesNotHang) {
    Script script;
    const auto camera = makeResilientCamera(script.factory(), fastRetry());
    ASSERT_TRUE(camera->open("SN1").has_value());
    script.failOpens = 1'000'000;
    script.withCurrent([](ScriptedCamera& cam) { cam.emitEvent(CameraEventKind::Offline); });
    ASSERT_TRUE(waitFor([&] { return camera->health().reconnectAttempts >= 2; }));

    camera->close();
    EXPECT_FALSE(camera->isOpen());
    EXPECT_EQ(camera->health().state, CameraState::Closed);
}

TEST(ResilientCamera, CanBeReopenedAfterClose) {
    Script script;
    const auto camera = makeResilientCamera(script.factory(), fastRetry());
    ASSERT_TRUE(camera->open("SN1").has_value());
    camera->close();
    ASSERT_TRUE(camera->open("SN1").has_value());
    EXPECT_EQ(camera->health().framesReceived, 0U);
    camera->close();
}

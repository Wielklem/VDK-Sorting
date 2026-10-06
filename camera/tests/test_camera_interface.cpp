#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include <vsort/camera/camera.hpp>

namespace {

using namespace vsort;
using namespace vsort::camera;

// Minimal in-memory camera: proves the interface is implementable and its contract is clear.
class FakeCamera final : public ICamera {
public:
    Result<> open(std::string_view serial) override {
        if (serial.empty()) {
            return makeError(Errc::InvalidArgument, "empty serial");
        }
        info_.serial = std::string{serial};
        open_ = true;
        return {};
    }

    void close() noexcept override {
        stop();
        open_ = false;
    }

    [[nodiscard]] bool isOpen() const noexcept override { return open_; }

    [[nodiscard]] Result<CameraInfo> info() const override {
        if (!open_) {
            return makeError(Errc::NotFound, "not open");
        }
        return info_;
    }

    [[nodiscard]] Result<CameraSettings> settings() const override {
        if (!open_) {
            return makeError(Errc::NotFound, "not open");
        }
        return settings_;
    }

    Result<> applySettings(const CameraSettings& settings) override {
        if (!open_) {
            return makeError(Errc::NotFound, "not open");
        }
        if (settings.exposureUs <= 0.0) {
            return makeError(Errc::InvalidArgument, "exposure must be > 0");
        }
        settings_ = settings;
        return {};
    }

    void setFrameCallback(FrameCallback callback) override { callback_ = std::move(callback); }

    Result<> start() override {
        if (!open_) {
            return makeError(Errc::NotFound, "not open");
        }
        streaming_ = true;
        return {};
    }

    void stop() noexcept override { streaming_ = false; }

    [[nodiscard]] bool isStreaming() const noexcept override { return streaming_; }

    // Test helper: what a grab thread would do.
    void emit(const Frame& frame) const {
        if (streaming_ && callback_) {
            callback_(frame);
        }
    }

private:
    bool open_{false};
    bool streaming_{false};
    CameraInfo info_;
    CameraSettings settings_;
    FrameCallback callback_;
};

} // namespace

TEST(ICamera, DefaultSettingsAreHardwareTrigger) {
    const CameraSettings s;
    EXPECT_EQ(s.triggerMode, TriggerMode::Hardware);
    EXPECT_EQ(s.triggerEdge, TriggerEdge::Rising);
}

TEST(ICamera, RequiresOpenBeforeStart) {
    FakeCamera cam;
    const auto r = cam.start();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Errc::NotFound);
    EXPECT_FALSE(cam.isStreaming());
}

TEST(ICamera, OpenRejectsEmptySerial) {
    FakeCamera cam;
    const auto r = cam.open("");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Errc::InvalidArgument);
}

TEST(ICamera, ApplyAndReadSettings) {
    FakeCamera cam;
    ASSERT_TRUE(cam.open("SN123").has_value());

    CameraSettings s;
    s.exposureUs = 500.0;
    s.gainDb = 3.0;
    s.roi = Roi{.x = 10, .y = 20, .width = 640, .height = 480};
    s.triggerMode = TriggerMode::Software;
    ASSERT_TRUE(cam.applySettings(s).has_value());

    const auto got = cam.settings();
    ASSERT_TRUE(got.has_value());
    EXPECT_DOUBLE_EQ(got->exposureUs, 500.0);
    EXPECT_EQ(got->roi.width, 640U);
    EXPECT_EQ(got->triggerMode, TriggerMode::Software);

    s.exposureUs = 0.0;
    const auto bad = cam.applySettings(s);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, Errc::InvalidArgument);
}

TEST(ICamera, DeliversFramesOnlyWhileStreaming) {
    FakeCamera cam;
    ASSERT_TRUE(cam.open("SN123").has_value());

    int calls = 0;
    std::uint64_t lastId = 0;
    cam.setFrameCallback([&](const Frame& f) {
        ++calls;
        lastId = f.meta.frameId.value();
    });

    const auto pixels = std::make_shared<std::array<std::byte, 4>>();
    Frame frame;
    frame.meta.frameId = FrameId{42};
    frame.meta.width = 2;
    frame.meta.height = 2;
    frame.meta.strideBytes = 2;
    frame.data = std::span<const std::byte>{*pixels};
    frame.owner = pixels;

    cam.emit(frame);
    EXPECT_EQ(calls, 0);

    ASSERT_TRUE(cam.start().has_value());
    cam.emit(frame);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(lastId, 42U);

    cam.close();
    EXPECT_FALSE(cam.isStreaming());
    EXPECT_FALSE(cam.isOpen());
    cam.emit(frame);
    EXPECT_EQ(calls, 1);
}

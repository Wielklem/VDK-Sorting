#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <gtest/gtest.h>

#include <vsort/camera/camera.hpp>
#include <vsort/ipc/envelope.hpp>
#include <vsort/platform/shared_memory.hpp>

#include "ipc/camera_access.hpp"

namespace vsort::testutil {

// Windows stubs (M15.20 not done) return NotSupported for shared memory: skip those tests.
inline bool sharedMemoryAvailable() {
    const auto probe = platform::createSharedMemory("vsort_test_probe", 64);
    return probe.has_value();
}
#define VSORT_SKIP_IF_NO_SHM()                                                                     \
    do {                                                                                           \
        if (!::vsort::testutil::sharedMemoryAvailable()) {                                         \
            GTEST_SKIP() << "shared memory not supported here";                                    \
        }                                                                                          \
    } while (false)

// Frame with every byte (all channels) = value. `padding` extra bytes per row.
struct TestFrame {
    std::shared_ptr<std::vector<std::byte>> storage{std::make_shared<std::vector<std::byte>>()};
    std::vector<std::byte>& bytes{*storage};
    camera::Frame frame;
};

inline std::shared_ptr<TestFrame> makeFrame(std::uint16_t cameraId, std::uint64_t frameId,
                                            std::uint32_t width, std::uint32_t height,
                                            std::uint8_t value,
                                            camera::PixelFormat format = camera::PixelFormat::Mono8,
                                            std::uint32_t padding = 0, bool withOwner = true) {
    const std::uint32_t channels =
        (format == camera::PixelFormat::Rgb8 || format == camera::PixelFormat::Bgr8) ? 3U : 1U;
    auto out = std::make_shared<TestFrame>();
    const std::uint32_t stride = width * channels + padding;
    out->bytes.assign(static_cast<std::size_t>(stride) * height, static_cast<std::byte>(value));
    out->frame.meta.frameId = FrameId{frameId};
    out->frame.meta.hostTimestamp = Timestamp::now();
    out->frame.meta.width = width;
    out->frame.meta.height = height;
    out->frame.meta.strideBytes = stride;
    out->frame.meta.pixelFormat = format;
    out->frame.meta.cameraIndex = cameraId;
    out->frame.data = out->bytes;
    if (withOwner) {
        out->frame.owner = out->storage;
    }
    return out;
}

class FakeCameras final : public service::ICameraAccess {
public:
    FakeCameras() {
        cameras_ = {{.id = 1,
                     .serial = "SN-A",
                     .model = "MER-A",
                     .state = service::CameraRuntimeState::Streaming},
                    {.id = 2,
                     .serial = "SN-B",
                     .model = "MER-B",
                     .state = service::CameraRuntimeState::Open}};
    }
    [[nodiscard]] std::vector<service::CameraListEntry> list() const override { return cameras_; }
    [[nodiscard]] Result<camera::CameraSettings> settings(std::uint16_t id) const override {
        const std::scoped_lock lock{mutex_};
        if (id != 1 && id != 2) {
            return makeError(Errc::NotFound, "no such camera");
        }
        const auto it = settings_.find(id);
        return it != settings_.end() ? it->second : camera::CameraSettings{};
    }
    [[nodiscard]] Result<> apply(std::uint16_t id, const camera::CameraSettings& s) override {
        if (id != 1 && id != 2) {
            return makeError(Errc::NotFound, "no such camera");
        }
        if (s.exposureUs <= 0.0) {
            return makeError(Errc::InvalidArgument, "exposure must be > 0");
        }
        const std::scoped_lock lock{mutex_};
        settings_[id] = s;
        return {};
    }

private:
    std::vector<service::CameraListEntry> cameras_;
    mutable std::mutex mutex_;
    std::map<std::uint16_t, camera::CameraSettings> settings_;
};

// Builds a request envelope around an already built payload.
inline std::vector<std::uint8_t> request(flatbuffers::FlatBufferBuilder& fbb, ipc::fb::MsgType type,
                                         std::uint64_t id, ipc::fb::Payload payloadType,
                                         flatbuffers::Offset<void> payload) {
    return ipc::finishEnvelope(
        fbb, {.type = type, .requestId = id, .timestampNs = 0, .status = 0, .errorText = {}},
        payloadType, payload);
}

} // namespace vsort::testutil

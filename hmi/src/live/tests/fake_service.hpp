#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <zmq.hpp>

#include <vsort/ipc/envelope.hpp>
#include <vsort/ipc/preview_ring.hpp>

namespace vsort::hmi::test {

// Minimal stand-in for the service IPC server (ROUTER + PUB + preview rings), driven by pump()
// from the test's own loop: no threads.
class FakeService {
public:
    static constexpr std::uint16_t kCommandPort = 25555;
    static constexpr std::uint16_t kEventPort = 25556;
    static constexpr std::uint32_t kWidth = 64;
    static constexpr std::uint32_t kHeight = 48;

    explicit FakeService(std::vector<std::uint16_t> cameraIds);

    // False when shared memory is not available on this platform (Windows stubs).
    [[nodiscard]] bool ringsAvailable() const noexcept { return ringsOk_; }

    void pump();        // answer commands, publish heartbeat
    void writeFrames(); // one new frame into every ring
    void setHeartbeats(bool on) { heartbeats_ = on; }
    void publishCameraListChanged(); // like the service when a camera appears or disappears

    [[nodiscard]] int cameraListRequests() const noexcept { return cameraListRequests_; }
    [[nodiscard]] int setPreviewRequests() const noexcept { return setPreviewRequests_; }
    // Exposure the fake camera currently has (changed by SetCameraSettings).
    [[nodiscard]] double exposureUs(std::uint16_t cameraId) const {
        for (const Cam& cam : cams_) {
            if (cam.id == cameraId) {
                return cam.exposureUs;
            }
        }
        return 0.0;
    }

private:
    struct Cam {
        std::uint16_t id{0};
        std::string shm;
        bool previewEnabled{false};
        double exposureUs{5000.0};
        double gainDb{2.0};
        std::unique_ptr<ipc::PreviewRingWriter> ring;
    };

    void handleCommand(zmq::message_t& identity, const std::vector<std::uint8_t>& request);
    void publishHeartbeat();
    [[nodiscard]] std::vector<std::uint8_t> configReply(flatbuffers::FlatBufferBuilder& fbb,
                                                        const ipc::EnvelopeFields& fields,
                                                        const std::string& module) const;

    [[nodiscard]] flatbuffers::Offset<ipc::fb::CameraEntry>
    entry(flatbuffers::FlatBufferBuilder& fbb, const Cam& cam) const;

    zmq::context_t context_{1};
    zmq::socket_t router_{context_, zmq::socket_type::router};
    zmq::socket_t pub_{context_, zmq::socket_type::pub};
    std::vector<Cam> cams_;
    bool ringsOk_{true};
    bool heartbeats_{true};
    std::chrono::steady_clock::time_point nextHeartbeat_{};
    std::uint64_t heartbeatSeq_{0};
    std::uint64_t frameSeq_{0};
    int cameraListRequests_{0};
    int setPreviewRequests_{0};
    std::map<std::string, std::string> configs_; // module -> JSON, like the service config store
    std::uint32_t configVersion_{1};
};

} // namespace vsort::hmi::test

#pragma once

#include <chrono>
#include <cstdint>
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

    [[nodiscard]] int cameraListRequests() const noexcept { return cameraListRequests_; }
    [[nodiscard]] int setPreviewRequests() const noexcept { return setPreviewRequests_; }

private:
    struct Cam {
        std::uint16_t id{0};
        std::string shm;
        bool previewEnabled{false};
        std::unique_ptr<ipc::PreviewRingWriter> ring;
    };

    void handleCommand(zmq::message_t& identity, const std::vector<std::uint8_t>& request);
    void publishHeartbeat();
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
};

} // namespace vsort::hmi::test

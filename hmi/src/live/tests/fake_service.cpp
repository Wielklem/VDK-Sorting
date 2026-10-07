#include "fake_service.hpp"

#include <cstddef>
#include <string_view>
#include <utility>

namespace vsort::hmi::test {
namespace fb = ipc::fb;

FakeService::FakeService(std::vector<std::uint16_t> cameraIds) {
    router_.set(zmq::sockopt::linger, 0);
    pub_.set(zmq::sockopt::linger, 0);
    router_.bind("tcp://127.0.0.1:" + std::to_string(kCommandPort));
    pub_.bind("tcp://127.0.0.1:" + std::to_string(kEventPort));
    for (const auto id : cameraIds) {
        Cam& cam = cams_.emplace_back();
        cam.id = id;
        cam.shm = ipc::previewRingName(id, 1);
        auto ring = ipc::PreviewRingWriter::create(
            cam.shm, ipc::RingSpec{.slotCount = 4, .slotSize = kWidth * kHeight, .generation = 1});
        if (ring) {
            cam.ring = std::move(*ring);
        } else {
            ringsOk_ = false;
        }
    }
}

flatbuffers::Offset<fb::CameraEntry> FakeService::entry(flatbuffers::FlatBufferBuilder& fbb,
                                                        const Cam& cam) const {
    const auto serial = fbb.CreateString("SN" + std::to_string(cam.id));
    const auto model = fbb.CreateString("FakeCam");
    const auto shm = fbb.CreateString(cam.previewEnabled ? cam.shm : std::string{});
    return fb::CreateCameraEntry(fbb, cam.id, serial, model, fb::CameraState::Streaming,
                                 cam.previewEnabled, 10, 640, shm, 1);
}

void FakeService::pump() {
    for (int i = 0; i < 16; ++i) {
        zmq::message_t identity;
        if (!router_.recv(identity, zmq::recv_flags::dontwait)) {
            break;
        }
        zmq::message_t body;
        if (!identity.more() || !router_.recv(body, zmq::recv_flags::dontwait)) {
            continue;
        }
        const auto* data = static_cast<const std::uint8_t*>(body.data());
        handleCommand(identity, std::vector<std::uint8_t>(data, data + body.size()));
    }
    if (heartbeats_ && std::chrono::steady_clock::now() >= nextHeartbeat_) {
        nextHeartbeat_ = std::chrono::steady_clock::now() + std::chrono::milliseconds{200};
        publishHeartbeat();
    }
}

void FakeService::handleCommand(zmq::message_t& identity,
                                const std::vector<std::uint8_t>& request) {
    const auto parsed = ipc::parseEnvelope(request);
    if (!parsed) {
        return;
    }
    const fb::Envelope& in = **parsed;
    flatbuffers::FlatBufferBuilder fbb{512};
    ipc::EnvelopeFields fields{.type = static_cast<fb::MsgType>(in.msg_type()),
                               .requestId = in.request_id(),
                               .timestampNs = 0,
                               .status = 0,
                               .errorText = {}};
    std::vector<std::uint8_t> reply;
    if (in.payload_type() == fb::Payload::GetCameraListRequest) {
        ++cameraListRequests_;
        std::vector<flatbuffers::Offset<fb::CameraEntry>> entries;
        for (const Cam& cam : cams_) {
            entries.push_back(entry(fbb, cam));
        }
        const auto list = fb::CreateCameraListReply(fbb, fbb.CreateVector(entries));
        reply = ipc::finishEnvelope(fbb, fields, fb::Payload::CameraListReply, list.Union());
    } else if (in.payload_type() == fb::Payload::SetPreviewRequest) {
        ++setPreviewRequests_;
        const auto* req = in.payload_as_SetPreviewRequest();
        for (Cam& cam : cams_) {
            if (cam.id == req->camera_id()) {
                cam.previewEnabled = req->enabled();
                const auto rep = fb::CreateSetPreviewReply(fbb, entry(fbb, cam));
                reply = ipc::finishEnvelope(fbb, fields, fb::Payload::SetPreviewReply, rep.Union());
                // Like the real service: the stream event follows the first frame.
                flatbuffers::FlatBufferBuilder ev{256};
                const auto shm = ev.CreateString(cam.shm);
                const auto event = fb::CreatePreviewStreamChangedEvent(
                    ev, cam.id, shm, 1, kWidth, kHeight, fb::PixelFormat::Mono8);
                const auto bytes =
                    ipc::finishEnvelope(ev,
                                        {.type = fb::MsgType::PreviewStreamChanged,
                                         .requestId = 0,
                                         .timestampNs = 0,
                                         .status = 0,
                                         .errorText = {}},
                                        fb::Payload::PreviewStreamChangedEvent, event.Union());
                (void)pub_.send(zmq::buffer(ipc::kTopicPreview), zmq::send_flags::sndmore);
                (void)pub_.send(zmq::buffer(bytes), zmq::send_flags::none);
                break;
            }
        }
    }
    if (in.payload_type() == fb::Payload::GetCameraSettingsRequest) {
        const auto* req = in.payload_as_GetCameraSettingsRequest();
        for (const Cam& cam : cams_) {
            if (cam.id == req->camera_id()) {
                const auto settings = fb::CreateCameraSettings(
                    fbb, cam.exposureUs, cam.gainDb, 0, 0, 0, 0, fb::TriggerMode::Hardware, true);
                const auto rep = fb::CreateCameraSettingsReply(fbb, cam.id, settings);
                reply =
                    ipc::finishEnvelope(fbb, fields, fb::Payload::CameraSettingsReply, rep.Union());
                break;
            }
        }
    }
    if (in.payload_type() == fb::Payload::SetCameraSettingsRequest) {
        const auto* req = in.payload_as_SetCameraSettingsRequest();
        for (Cam& cam : cams_) {
            if (cam.id == req->camera_id() && req->settings() != nullptr) {
                cam.exposureUs = req->settings()->exposure_us();
                cam.gainDb = req->settings()->gain_db();
                reply = ipc::finishEnvelope(fbb, fields, fb::Payload::NONE, {});
                break;
            }
        }
    }
    if (in.payload_type() == fb::Payload::GetConfigRequest) {
        const auto* req = in.payload_as_GetConfigRequest();
        const std::string module =
            req->module_name() != nullptr ? req->module_name()->str() : std::string{};
        reply = configReply(fbb, fields, module);
    }
    if (in.payload_type() == fb::Payload::SetConfigRequest) {
        const auto* req = in.payload_as_SetConfigRequest();
        const std::string module =
            req->module_name() != nullptr ? req->module_name()->str() : std::string{};
        configs_[module] = req->json() != nullptr ? req->json()->str() : std::string{};
        ++configVersion_;
        reply = configReply(fbb, fields, module);
    }
    if (reply.empty()) {
        fbb.Clear();
        fields.status = 1;
        fields.errorText = "unsupported";
        reply = ipc::finishEnvelope(fbb, fields, fb::Payload::NONE, {});
    }
    (void)router_.send(std::move(identity), zmq::send_flags::sndmore);
    (void)router_.send(zmq::buffer(reply), zmq::send_flags::none);
}

std::vector<std::uint8_t> FakeService::configReply(flatbuffers::FlatBufferBuilder& fbb,
                                                   const ipc::EnvelopeFields& fields,
                                                   const std::string& module) const {
    const auto it = configs_.find(module);
    const std::string json = it != configs_.end() ? it->second : std::string{R"({"cameras":[]})"};
    const auto reply = fb::CreateConfigReply(fbb, fbb.CreateString(module), fbb.CreateString(json),
                                             configVersion_);
    return ipc::finishEnvelope(fbb, fields, fb::Payload::ConfigReply, reply.Union());
}

void FakeService::publishCameraListChanged() {
    flatbuffers::FlatBufferBuilder fbb{64};
    const auto event = fb::CreateCameraListChangedEvent(fbb);
    const auto bytes = ipc::finishEnvelope(fbb,
                                           {.type = fb::MsgType::CameraListChanged,
                                            .requestId = 0,
                                            .timestampNs = 0,
                                            .status = 0,
                                            .errorText = {}},
                                           fb::Payload::CameraListChangedEvent, event.Union());
    (void)pub_.send(zmq::buffer(ipc::kTopicCamera), zmq::send_flags::sndmore);
    (void)pub_.send(zmq::buffer(bytes), zmq::send_flags::none);
}

void FakeService::publishHeartbeat() {
    flatbuffers::FlatBufferBuilder fbb{128};
    const auto event =
        fb::CreateHeartbeatEvent(fbb, fb::ServiceState::Running, 1000, ++heartbeatSeq_);
    const auto bytes = ipc::finishEnvelope(fbb,
                                           {.type = fb::MsgType::Heartbeat,
                                            .requestId = 0,
                                            .timestampNs = 0,
                                            .status = 0,
                                            .errorText = {}},
                                           fb::Payload::HeartbeatEvent, event.Union());
    (void)pub_.send(zmq::buffer(ipc::kTopicHeartbeat), zmq::send_flags::sndmore);
    (void)pub_.send(zmq::buffer(bytes), zmq::send_flags::none);
}

void FakeService::writeFrames() {
    ++frameSeq_;
    for (Cam& cam : cams_) {
        if (!cam.ring || !cam.previewEnabled) {
            continue;
        }
        std::vector<std::byte> pixels(static_cast<std::size_t>(kWidth) * kHeight);
        for (std::uint32_t y = 0; y < kHeight; ++y) {
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                // Gradient that moves with the frame number; camera id shifts the pattern.
                pixels[y * kWidth + x] =
                    static_cast<std::byte>((x * 4 + y + frameSeq_ * 7 + cam.id * 40) & 0xFF);
            }
        }
        (void)cam.ring->write(ipc::PreviewFrameInfo{.frameId = frameSeq_,
                                                    .timestampNs = frameSeq_,
                                                    .width = kWidth,
                                                    .height = kHeight,
                                                    .strideBytes = kWidth,
                                                    .pixelFormat = ipc::PreviewPixelFormat::Mono8},
                              pixels);
    }
}

} // namespace vsort::hmi::test

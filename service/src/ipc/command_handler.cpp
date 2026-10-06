#include "ipc/command_handler.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace vsort::service {
namespace {

namespace fb = ipc::fb;
using Fbb = flatbuffers::FlatBufferBuilder;

flatbuffers::Offset<flatbuffers::String> str(Fbb& fbb, std::string_view text) {
    return fbb.CreateString(text.data(), text.size());
}

std::string_view view(const flatbuffers::String* s) noexcept {
    return s != nullptr ? std::string_view{s->c_str(), s->size()} : std::string_view{};
}

fb::CameraState toFb(CameraRuntimeState state) noexcept {
    switch (state) {
    case CameraRuntimeState::Open:
        return fb::CameraState::Open;
    case CameraRuntimeState::Streaming:
        return fb::CameraState::Streaming;
    case CameraRuntimeState::Reconnecting:
        return fb::CameraState::Reconnecting;
    case CameraRuntimeState::Closed:
        break;
    }
    return fb::CameraState::Closed;
}

flatbuffers::Offset<fb::CameraEntry> makeEntry(Fbb& fbb, const CameraListEntry& camera,
                                               const PreviewSettings& settings,
                                               const PreviewStream& stream) {
    const auto serial = str(fbb, camera.serial);
    const auto model = str(fbb, camera.model);
    const auto shm = str(fbb, stream.shmName);
    return fb::CreateCameraEntry(fbb, camera.id, serial, model, toFb(camera.state),
                                 settings.enabled, settings.fps, settings.maxWidth, shm,
                                 stream.generation);
}

flatbuffers::Offset<fb::CameraSettings> makeSettings(Fbb& fbb, const camera::CameraSettings& s) {
    return fb::CreateCameraSettings(
        fbb, s.exposureUs, s.gainDb, s.roi.x, s.roi.y, s.roi.width, s.roi.height,
        static_cast<fb::TriggerMode>(static_cast<std::uint8_t>(s.triggerMode)),
        s.triggerEdge == camera::TriggerEdge::Rising);
}

Result<camera::CameraSettings> fromFb(const fb::CameraSettings* in) {
    if (in == nullptr) {
        return makeError(Errc::InvalidArgument, "settings missing");
    }
    camera::CameraSettings out;
    out.exposureUs = in->exposure_us();
    out.gainDb = in->gain_db();
    out.roi = camera::Roi{in->roi_x(), in->roi_y(), in->roi_width(), in->roi_height()};
    switch (in->trigger_mode()) {
    case fb::TriggerMode::FreeRun:
        out.triggerMode = camera::TriggerMode::FreeRun;
        break;
    case fb::TriggerMode::Software:
        out.triggerMode = camera::TriggerMode::Software;
        break;
    case fb::TriggerMode::Hardware:
        out.triggerMode = camera::TriggerMode::Hardware;
        break;
    default:
        return makeError(Errc::InvalidArgument, "unknown trigger mode");
    }
    out.triggerEdge =
        in->trigger_rising() ? camera::TriggerEdge::Rising : camera::TriggerEdge::Falling;
    return out;
}

std::optional<CameraListEntry> findCamera(const ICameraAccess& cameras, std::uint16_t id) {
    const auto all = cameras.list();
    const auto it = std::ranges::find(all, id, &CameraListEntry::id);
    if (it == all.end()) {
        return std::nullopt;
    }
    return *it;
}

} // namespace

CommandHandler::CommandHandler(PreviewHub& hub, ICameraAccess& cameras, IConfigStore* config,
                               std::string serviceVersion)
    : hub_{hub}
    , cameras_{cameras}
    , config_{config}
    , serviceVersion_{std::move(serviceVersion)}
    , started_{Timestamp::now()} {}

std::uint64_t CommandHandler::uptimeMs() const noexcept {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(Timestamp::now() - started_);
    return static_cast<std::uint64_t>(std::max<std::int64_t>(elapsed.count(), 0));
}

std::vector<std::uint8_t> CommandHandler::handle(std::span<const std::uint8_t> request) const {
    Fbb fbb{1024};
    ipc::EnvelopeFields fields;
    fields.timestampNs = static_cast<std::uint64_t>(Timestamp::now().ns());

    const auto parsed = ipc::parseEnvelope(request);
    if (!parsed) {
        fields.status = static_cast<std::uint16_t>(parsed.error().code);
        fields.errorText = parsed.error().message;
        return ipc::finishEnvelope(fbb, fields, fb::Payload::NONE, {});
    }
    const fb::Envelope& envelope = **parsed;
    fields.type = static_cast<fb::MsgType>(envelope.msg_type());
    fields.requestId = envelope.request_id();

    auto result = dispatch(fbb, envelope);
    if (!result) {
        fbb.Clear();
        fields.status = static_cast<std::uint16_t>(result.error().code);
        fields.errorText = result.error().message;
        return ipc::finishEnvelope(fbb, fields, fb::Payload::NONE, {});
    }
    return ipc::finishEnvelope(fbb, fields, result->type, result->body);
}

Result<CommandHandler::Reply> CommandHandler::dispatch(Fbb& fbb,
                                                       const fb::Envelope& request) const {
    switch (request.payload_type()) {
    case fb::Payload::HelloRequest:
        return onHello(fbb);
    case fb::Payload::GetCameraListRequest:
        return onGetCameraList(fbb);
    case fb::Payload::SetPreviewRequest:
        return onSetPreview(fbb, *request.payload_as_SetPreviewRequest());
    case fb::Payload::GetCameraSettingsRequest:
        return onGetCameraSettings(fbb, *request.payload_as_GetCameraSettingsRequest());
    case fb::Payload::SetCameraSettingsRequest:
        return onSetCameraSettings(*request.payload_as_SetCameraSettingsRequest());
    case fb::Payload::GetConfigRequest:
        return onGetConfig(fbb, *request.payload_as_GetConfigRequest());
    case fb::Payload::SetConfigRequest:
        return onSetConfig(fbb, *request.payload_as_SetConfigRequest());
    default:
        return makeError(Errc::NotSupported, "unknown or unsupported command");
    }
}

Result<CommandHandler::Reply> CommandHandler::onHello(Fbb& fbb) const {
    const std::vector<flatbuffers::Offset<flatbuffers::String>> caps{
        str(fbb, "preview"), str(fbb, "camera_settings"), str(fbb, "config")};
    const auto reply =
        fb::CreateHelloReply(fbb, str(fbb, serviceVersion_), fb::ServiceState::Running, uptimeMs(),
                             fbb.CreateVector(caps));
    return Reply{fb::Payload::HelloReply, reply.Union()};
}

Result<CommandHandler::Reply> CommandHandler::onGetCameraList(Fbb& fbb) const {
    std::vector<flatbuffers::Offset<fb::CameraEntry>> entries;
    for (const auto& camera : cameras_.list()) {
        entries.push_back(makeEntry(fbb, camera, hub_.settings(camera.id), hub_.stream(camera.id)));
    }
    const auto reply = fb::CreateCameraListReply(fbb, fbb.CreateVector(entries));
    return Reply{fb::Payload::CameraListReply, reply.Union()};
}

// fps == 0 or max_width == 0 means "keep the current value".
Result<CommandHandler::Reply> CommandHandler::onSetPreview(Fbb& fbb,
                                                           const fb::SetPreviewRequest& req) const {
    const auto camera = findCamera(cameras_, req.camera_id());
    if (!camera) {
        return makeError(Errc::NotFound, "unknown camera id " + std::to_string(req.camera_id()));
    }
    PreviewSettings wanted = hub_.settings(camera->id);
    wanted.enabled = req.enabled();
    if (req.fps() != 0) {
        wanted.fps = req.fps();
    }
    if (req.max_width() != 0) {
        wanted.maxWidth = req.max_width();
    }
    const auto applied = hub_.setPreview(camera->id, wanted);
    if (!applied) {
        return std::unexpected{applied.error()};
    }
    const auto entry = makeEntry(fbb, *camera, *applied, hub_.stream(camera->id));
    return Reply{fb::Payload::SetPreviewReply, fb::CreateSetPreviewReply(fbb, entry).Union()};
}

Result<CommandHandler::Reply>
CommandHandler::onGetCameraSettings(Fbb& fbb, const fb::GetCameraSettingsRequest& req) const {
    const auto settings = cameras_.settings(req.camera_id());
    if (!settings) {
        return std::unexpected{settings.error()};
    }
    const auto reply =
        fb::CreateCameraSettingsReply(fbb, req.camera_id(), makeSettings(fbb, *settings));
    return Reply{fb::Payload::CameraSettingsReply, reply.Union()};
}

Result<CommandHandler::Reply>
CommandHandler::onSetCameraSettings(const fb::SetCameraSettingsRequest& req) const {
    const auto settings = fromFb(req.settings());
    if (!settings) {
        return std::unexpected{settings.error()};
    }
    if (const auto applied = cameras_.apply(req.camera_id(), *settings); !applied) {
        return std::unexpected{applied.error()};
    }
    return Reply{};
}

Result<CommandHandler::Reply> CommandHandler::onGetConfig(Fbb& fbb,
                                                          const fb::GetConfigRequest& req) const {
    if (config_ == nullptr) {
        return makeError(Errc::NotSupported, "no config store");
    }
    const std::string module{view(req.module_name())};
    const auto data = config_->get(module);
    if (!data) {
        return std::unexpected{data.error()};
    }
    const auto version = config_->version(module);
    if (!version) {
        return std::unexpected{version.error()};
    }
    const auto reply =
        fb::CreateConfigReply(fbb, str(fbb, module), str(fbb, data->dump()), version->number);
    return Reply{fb::Payload::ConfigReply, reply.Union()};
}

Result<CommandHandler::Reply> CommandHandler::onSetConfig(Fbb& fbb,
                                                          const fb::SetConfigRequest& req) const {
    if (config_ == nullptr) {
        return makeError(Errc::NotSupported, "no config store");
    }
    const std::string module{view(req.module_name())};
    auto json = nlohmann::json::parse(view(req.json()), nullptr, false);
    if (json.is_discarded()) {
        return makeError(Errc::ParseError, "config is not valid JSON");
    }
    const std::string_view author =
        req.author() != nullptr && req.author()->size() > 0 ? view(req.author()) : "hmi";
    const auto version = config_->set(module, std::move(json), author);
    if (!version) {
        return std::unexpected{version.error()};
    }
    const auto stored = config_->get(module);
    if (!stored) {
        return std::unexpected{stored.error()};
    }
    const auto reply =
        fb::CreateConfigReply(fbb, str(fbb, module), str(fbb, stored->dump()), version->number);
    return Reply{fb::Payload::ConfigReply, reply.Union()};
}

} // namespace vsort::service

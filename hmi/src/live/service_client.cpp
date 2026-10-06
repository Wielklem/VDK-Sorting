#include "live/service_client.hpp"

#include <QDebug>
#include <string>
#include <utility>

namespace vsort::hmi {
namespace {

namespace fb = ipc::fb;
using namespace std::chrono_literals;

constexpr int kPollIntervalMs = 20;
constexpr int kMaxMessagesPerPoll = 64;
constexpr auto kHeartbeatTimeout = 3s;

QString toQString(const flatbuffers::String* text) {
    return text != nullptr ? QString::fromUtf8(text->c_str(), static_cast<qsizetype>(text->size()))
                           : QString{};
}

CameraLinkState toLinkState(fb::CameraState state) noexcept {
    switch (state) {
    case fb::CameraState::Open:
        return CameraLinkState::Open;
    case fb::CameraState::Streaming:
        return CameraLinkState::Streaming;
    case fb::CameraState::Reconnecting:
        return CameraLinkState::Reconnecting;
    default:
        return CameraLinkState::Closed;
    }
}

CameraInfo toCameraInfo(const fb::CameraEntry& entry) {
    CameraInfo info;
    info.id = entry.id();
    info.serial = toQString(entry.serial());
    info.model = toQString(entry.model());
    info.state = toLinkState(entry.state());
    info.previewEnabled = entry.preview_enabled();
    info.shmName = toQString(entry.preview_shm_name());
    info.generation = entry.preview_generation();
    return info;
}

std::string endpoint(const QString& host, quint16 port) {
    return "tcp://" + host.toStdString() + ":" + std::to_string(port);
}

} // namespace

ServiceClient::ServiceClient(ServiceEndpoints endpoints, QObject* parent)
    : QObject{parent}
    , endpoints_{std::move(endpoints)} {
    timer_.setInterval(kPollIntervalMs);
    connect(&timer_, &QTimer::timeout, this, &ServiceClient::poll);
}

ServiceClient::~ServiceClient() = default;

void ServiceClient::start() {
    if (started_) {
        return;
    }
    try {
        dealer_.set(zmq::sockopt::linger, 0);
        sub_.set(zmq::sockopt::linger, 0);
        sub_.set(zmq::sockopt::subscribe, "");
        dealer_.connect(endpoint(endpoints_.host, endpoints_.commandPort));
        sub_.connect(endpoint(endpoints_.host, endpoints_.eventPort));
    } catch (const zmq::error_t& ex) {
        emit commandFailed(QStringLiteral("cannot connect: %1").arg(QString::fromUtf8(ex.what())));
        return;
    }
    started_ = true;
    timer_.start();
}

void ServiceClient::requestCameraList() {
    flatbuffers::FlatBufferBuilder fbb{128};
    const auto request = fb::CreateGetCameraListRequest(fbb);
    sendRequest(fb::MsgType::GetCameraList, fb::Payload::GetCameraListRequest, fbb,
                request.Union());
}

void ServiceClient::setPreview(quint16 cameraId, bool enabled) {
    flatbuffers::FlatBufferBuilder fbb{128};
    // fps = 0 and max_width = 0: keep the values the service already has.
    const auto request = fb::CreateSetPreviewRequest(fbb, cameraId, enabled, 0, 0);
    sendRequest(fb::MsgType::SetPreview, fb::Payload::SetPreviewRequest, fbb, request.Union());
}

void ServiceClient::sendRequest(fb::MsgType type, fb::Payload payloadType,
                                flatbuffers::FlatBufferBuilder& fbb,
                                flatbuffers::Offset<void> payload) {
    if (!started_) {
        return;
    }
    const auto bytes = ipc::finishEnvelope(fbb,
                                           {.type = type,
                                            .requestId = nextRequestId_++,
                                            .timestampNs = 0,
                                            .status = 0,
                                            .errorText = {}},
                                           payloadType, payload);
    try {
        // DEALER -> ROUTER: the service sees [identity][envelope].
        (void)dealer_.send(zmq::buffer(bytes), zmq::send_flags::dontwait);
    } catch (const zmq::error_t& ex) {
        qWarning() << "ipc send failed:" << ex.what();
    }
}

void ServiceClient::poll() {
    try {
        drain(dealer_, false);
        drain(sub_, true);
    } catch (const zmq::error_t& ex) {
        qWarning() << "ipc receive failed:" << ex.what();
    }
    checkHeartbeat();
}

void ServiceClient::drain(zmq::socket_t& socket, bool hasTopic) {
    for (int i = 0; i < kMaxMessagesPerPoll; ++i) {
        zmq::message_t message;
        if (hasTopic) { // PUB frames: [topic][envelope]
            zmq::message_t topic;
            if (!socket.recv(topic, zmq::recv_flags::dontwait)) {
                return;
            }
            if (!topic.more()) {
                continue;
            }
        }
        if (!socket.recv(message, zmq::recv_flags::dontwait)) {
            return;
        }
        // ZeroMQ buffers are not 8-byte aligned (FlatBuffers needs that): copy.
        const auto* data = static_cast<const std::uint8_t*>(message.data());
        const std::vector<std::uint8_t> bytes(data, data + message.size());
        handleMessage(bytes);
    }
}

void ServiceClient::handleMessage(std::span<const std::uint8_t> bytes) {
    const auto parsed = ipc::parseEnvelope(bytes);
    if (!parsed) {
        qWarning() << "ipc: dropped invalid message:"
                   << QString::fromStdString(std::string{parsed.error().what()});
        return;
    }
    const fb::Envelope& envelope = **parsed;
    if (envelope.protocol_version() != ipc::kProtocolVersion) {
        emit commandFailed(QStringLiteral("service protocol version %1, HMI expects %2")
                               .arg(envelope.protocol_version())
                               .arg(ipc::kProtocolVersion));
        return;
    }
    if (envelope.status() != 0) {
        emit commandFailed(toQString(envelope.error_text()));
        return;
    }
    switch (envelope.payload_type()) {
    case fb::Payload::HeartbeatEvent:
        lastHeartbeat_ = std::chrono::steady_clock::now();
        setConnected(true);
        break;
    case fb::Payload::CameraListReply: {
        QVector<CameraInfo> cameras;
        if (const auto* list = envelope.payload_as_CameraListReply()->cameras(); list != nullptr) {
            for (const auto* entry : *list) {
                cameras.push_back(toCameraInfo(*entry));
            }
        }
        emit cameraListReceived(cameras);
        break;
    }
    case fb::Payload::SetPreviewReply:
        if (const auto* entry = envelope.payload_as_SetPreviewReply()->camera(); entry != nullptr) {
            emit cameraUpdated(toCameraInfo(*entry));
        }
        break;
    case fb::Payload::PreviewStreamChangedEvent: {
        const auto* event = envelope.payload_as_PreviewStreamChangedEvent();
        emit streamChanged(event->camera_id(), toQString(event->shm_name()), event->generation());
        break;
    }
    default:
        break; // not needed by the live view
    }
}

void ServiceClient::checkHeartbeat() {
    if (connected_ && std::chrono::steady_clock::now() - lastHeartbeat_ > kHeartbeatTimeout) {
        setConnected(false);
    }
}

void ServiceClient::setConnected(bool connected) {
    if (connected_ == connected) {
        return;
    }
    connected_ = connected;
    emit connectedChanged(connected_);
}

} // namespace vsort::hmi

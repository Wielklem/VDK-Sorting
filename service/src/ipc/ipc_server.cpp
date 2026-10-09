#include "ipc/ipc_server.hpp"

#include <cerrno>
#include <string_view>

#include <spdlog/spdlog.h>

#include <vsort/common/version.hpp>

#include "ipc/cup_messages.hpp"
#include "ipc/overlay_messages.hpp"
#include "ipc/rate_messages.hpp"

namespace vsort::service {
namespace {

namespace fb = ipc::fb;
using namespace std::chrono_literals;

constexpr std::size_t kMaxRequestBytes = 1U << 20; // 1 MiB
constexpr int kMaxCommandsPerLoop = 16;

std::string endpoint(const IpcConfig& config, std::uint16_t port) {
    return "tcp://" + config.bindAddress + ":" + std::to_string(port);
}

} // namespace

IpcServer::IpcServer(IpcConfig config, std::unique_ptr<ICameraAccess> cameras,
                     IConfigStore* configStore)
    : config_{std::move(config)}
    , cameras_{cameras ? std::move(cameras) : std::make_unique<NullCameraAccess>()}
    , hub_{config_.preview, [this](const PreviewStream& stream) { onStreamChanged(stream); }}
    , handler_{hub_, *cameras_, configStore, std::string{version()}} {}

IpcServer::~IpcServer() {
    stop();
}

Result<> IpcServer::init(ModuleContext& context) {
    configSub_ = context.bus.subscribe<ConfigChanged>(64);
    cupSub_ = context.bus.subscribe<CupUpdate>(256);
    ratesSub_ = context.bus.subscribe<CameraRates>(16);
    overlaySub_ = context.bus.subscribe<AnalysisOverlay>(64);
    return {};
}

Result<> IpcServer::start() {
    if (running_) {
        return makeError(Errc::AlreadyExists, "ipc server already started");
    }
    try {
        router_ = zmq::socket_t{context_, zmq::socket_type::router};
        router_.set(zmq::sockopt::linger, 0);
        router_.set(zmq::sockopt::maxmsgsize, static_cast<std::int64_t>(kMaxRequestBytes));
        router_.bind(endpoint(config_, config_.commandPort));

        pub_ = zmq::socket_t{context_, zmq::socket_type::pub};
        pub_.set(zmq::sockopt::linger, 0);
        pub_.bind(endpoint(config_, config_.eventPort));
    } catch (const zmq::error_t& ex) {
        router_.close();
        pub_.close();
        return makeError(Errc::IoError, std::string{"cannot bind IPC sockets ("} +
                                            endpoint(config_, config_.commandPort) + ", " +
                                            endpoint(config_, config_.eventPort) +
                                            "): " + ex.what());
    }
    if (const auto started = hub_.start(); !started) {
        router_.close();
        pub_.close();
        return started;
    }
    failed_ = false;
    running_ = true;
    thread_ = std::jthread{[this](const std::stop_token& token) { run(token); }};
    spdlog::info("ipc: commands on {}, events on {}", endpoint(config_, config_.commandPort),
                 endpoint(config_, config_.eventPort));
    return {};
}

void IpcServer::stop() noexcept {
    try {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
        hub_.stop();
        running_ = false;
        router_.close();
        pub_.close();
    } catch (...) { // NOLINT(bugprone-empty-catch): stop() must not throw
    }
}

Health IpcServer::health() const {
    if (failed_) {
        return Health{HealthState::Failed, "ipc thread stopped unexpectedly"};
    }
    return Health{running_ ? HealthState::Ok : HealthState::Unknown, {}};
}

void IpcServer::run(const std::stop_token& stop) {
    auto nextHeartbeat = std::chrono::steady_clock::now();
    std::uint64_t heartbeatSeq = 0;
    while (!stop.stop_requested()) {
        try {
            zmq::pollitem_t items[] = {{router_.handle(), 0, ZMQ_POLLIN, 0}};
            zmq::poll(items, 1, 50ms);
            if ((items[0].revents & ZMQ_POLLIN) != 0) {
                serviceCommands();
            }
            drainBus();
            drainOutbox();

            if (std::chrono::steady_clock::now() >= nextHeartbeat) {
                nextHeartbeat += config_.heartbeatInterval;
                flatbuffers::FlatBufferBuilder fbb{128};
                const auto event = fb::CreateHeartbeatEvent(fbb, fb::ServiceState::Running,
                                                            handler_.uptimeMs(), ++heartbeatSeq);
                publish(ipc::kTopicHeartbeat,
                        ipc::finishEnvelope(
                            fbb,
                            {.type = fb::MsgType::Heartbeat,
                             .requestId = 0,
                             .timestampNs = static_cast<std::uint64_t>(Timestamp::now().ns()),
                             .status = 0,
                             .errorText = {}},
                            fb::Payload::HeartbeatEvent, event.Union()));
            }
        } catch (const zmq::error_t& ex) {
            if (ex.num() == ETERM) {
                break;
            }
            spdlog::warn("ipc: {}", ex.what());
        } catch (const std::exception& ex) {
            spdlog::error("ipc: unexpected error, stopping IPC thread: {}", ex.what());
            failed_ = true;
            break;
        }
    }
}

// Request: [identity][envelope] (DEALER on the other side). Reply goes to the same identity.
void IpcServer::serviceCommands() {
    for (int i = 0; i < kMaxCommandsPerLoop; ++i) {
        zmq::message_t identity;
        if (!router_.recv(identity, zmq::recv_flags::dontwait)) {
            return;
        }
        zmq::message_t payload;
        if (!identity.more() || !router_.recv(payload, zmq::recv_flags::dontwait)) {
            continue; // malformed: no body
        }
        while (payload.more()) { // ignore extra frames
            zmq::message_t extra;
            if (!router_.recv(extra, zmq::recv_flags::dontwait)) {
                break;
            }
        }
        // ZeroMQ buffers are not aligned for FlatBuffers: copy.
        const auto* data = static_cast<const std::uint8_t*>(payload.data());
        const std::vector<std::uint8_t> request(data, data + payload.size());
        const auto reply = handler_.handle(request);
        (void)router_.send(identity, zmq::send_flags::sndmore);
        (void)router_.send(zmq::buffer(reply), zmq::send_flags::none);
    }
}

void IpcServer::drainBus() {
    if (cupSub_) {
        cupSub_->drain([this](const CupUpdate& update) {
            publish(ipc::kTopicCups, makeCupUpdateEnvelope(update));
        });
    }
    if (ratesSub_) {
        ratesSub_->drain([this](const CameraRates& rates) {
            publish(ipc::kTopicCamera, makeCameraRatesEnvelope(rates));
        });
    }
    if (overlaySub_) {
        overlaySub_->drain([this](const AnalysisOverlay& overlay) {
            publish(ipc::kTopicAnalysis, makeAnalysisOverlayEnvelope(overlay));
        });
    }
    if (!configSub_) {
        return;
    }
    configSub_->drain([this](const ConfigChanged& changed) {
        flatbuffers::FlatBufferBuilder fbb{128};
        const auto name = fbb.CreateString(changed.module);
        const auto event = fb::CreateConfigChangedEvent(fbb, name, changed.version.number);
        publish(
            ipc::kTopicConfig,
            ipc::finishEnvelope(fbb,
                                {.type = fb::MsgType::ConfigChanged,
                                 .requestId = 0,
                                 .timestampNs = static_cast<std::uint64_t>(Timestamp::now().ns()),
                                 .status = 0,
                                 .errorText = {}},
                                fb::Payload::ConfigChangedEvent, event.Union()));
    });
}

void IpcServer::drainOutbox() {
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> batch;
    {
        const std::scoped_lock lock{outboxMutex_};
        batch.swap(outbox_);
    }
    for (const auto& [topic, message] : batch) {
        publish(topic, message);
    }
}

void IpcServer::publish(std::string_view topic, const std::vector<std::uint8_t>& message) {
    (void)pub_.send(zmq::buffer(topic.data(), topic.size()), zmq::send_flags::sndmore);
    (void)pub_.send(zmq::buffer(message), zmq::send_flags::none);
}

void IpcServer::enqueue(std::string topic, std::vector<std::uint8_t> message) {
    const std::scoped_lock lock{outboxMutex_};
    outbox_.emplace_back(std::move(topic), std::move(message));
}

void IpcServer::notifyCamerasChanged() {
    flatbuffers::FlatBufferBuilder fbb{64};
    const auto event = fb::CreateCameraListChangedEvent(fbb);
    enqueue(std::string{ipc::kTopicCamera},
            ipc::finishEnvelope(fbb,
                                {.type = fb::MsgType::CameraListChanged,
                                 .requestId = 0,
                                 .timestampNs = static_cast<std::uint64_t>(Timestamp::now().ns()),
                                 .status = 0,
                                 .errorText = {}},
                                fb::Payload::CameraListChangedEvent, event.Union()));
}

// Runs on the preview hub's worker thread: hand over to the I/O thread (sockets are not

// thread-safe).
void IpcServer::onStreamChanged(const PreviewStream& stream) {
    flatbuffers::FlatBufferBuilder fbb{256};
    const auto name = fbb.CreateString(stream.shmName);
    const auto event = fb::CreatePreviewStreamChangedEvent(
        fbb, stream.cameraId, name, stream.generation, stream.width, stream.height,
        static_cast<fb::PixelFormat>(static_cast<std::uint32_t>(stream.format)));
    enqueue(std::string{ipc::kTopicPreview},
            ipc::finishEnvelope(fbb,
                                {.type = fb::MsgType::PreviewStreamChanged,
                                 .requestId = 0,
                                 .timestampNs = static_cast<std::uint64_t>(Timestamp::now().ns()),
                                 .status = 0,
                                 .errorText = {}},
                                fb::Payload::PreviewStreamChangedEvent, event.Union()));
}

} // namespace vsort::service

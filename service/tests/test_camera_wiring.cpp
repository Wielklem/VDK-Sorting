#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <zmq.hpp>

#include <vsort/camera/camera_map.hpp>
#include <vsort/camera/camera_settings_config.hpp>
#include <vsort/common/module_registry.hpp>

#include "camera/camera_manager.hpp"
#include "camera/camera_module.hpp"
#include "ipc/ipc_server.hpp"
#include "test_camera_fakes.hpp"
#include "test_ipc_util.hpp"

using namespace vsort;
using namespace vsort::service;
using namespace std::chrono_literals;
using vsort::testutil::request;
using vsort::testutil::waitFor;
namespace fb = vsort::ipc::fb;

namespace {

constexpr std::uint16_t kCmdPort = 47655;
constexpr std::uint16_t kEvtPort = 47656;

struct Event {
    std::string topic;
    std::vector<std::uint8_t> bytes;
    const fb::Envelope* env{nullptr};
};

std::optional<Event> recvEvent(zmq::socket_t& sub, std::chrono::milliseconds timeout) {
    zmq::pollitem_t item{sub.handle(), 0, ZMQ_POLLIN, 0};
    zmq::poll(&item, 1, timeout);
    if ((item.revents & ZMQ_POLLIN) == 0) {
        return std::nullopt;
    }
    zmq::message_t topic;
    zmq::message_t body;
    if (!sub.recv(topic) || !sub.recv(body)) {
        return std::nullopt;
    }
    Event e;
    e.topic.assign(static_cast<const char*>(topic.data()), topic.size());
    const auto* d = static_cast<const std::uint8_t*>(body.data());
    e.bytes.assign(d, d + body.size());
    const auto env = ipc::parseEnvelope(e.bytes);
    if (!env) {
        return std::nullopt;
    }
    e.env = *env;
    return e;
}

std::optional<Event> waitTopic(zmq::socket_t& sub, std::string_view topic,
                               std::chrono::milliseconds total) {
    const auto end = std::chrono::steady_clock::now() + total;
    while (std::chrono::steady_clock::now() < end) {
        auto e = recvEvent(sub, 100ms);
        if (e && e->topic == topic) {
            return e;
        }
    }
    return std::nullopt;
}

// IPC server + camera module + fake cameras, talked to over real ZeroMQ sockets.
class CameraWiringTest : public ::testing::Test {
protected:
    void SetUp() override {
        VSORT_SKIP_IF_NO_SHM();
        dir_ = std::filesystem::temp_directory_path() / "vsort_camera_wiring_test";
        std::filesystem::remove_all(dir_);
        store_ = std::make_unique<FileConfigStore>(dir_, bus_);
        ASSERT_TRUE(camera::registerCameraMap(*store_).has_value());
        ASSERT_TRUE(camera::registerCameraSettings(*store_).has_value());
        const auto map =
            camera::CameraMap::create({{.serial = "SN-A", .id = 0}, {.serial = "SN-B", .id = 1}});
        ASSERT_TRUE(store_->set(camera::kCameraMapModule, map->toJson(), "test").has_value());
        world_.plug("SN-A");

        CameraManagerOptions options;
        options.scanInterval = 20ms;
        auto manager = std::make_shared<CameraManager>(
            std::make_unique<testutil::FakeBackend>(world_), store_.get(), options);
        IpcConfig config;
        config.commandPort = kCmdPort;
        config.eventPort = kEvtPort;
        config.heartbeatInterval = 200ms;
        auto ipc = std::make_unique<IpcServer>(
            config, std::make_unique<ManagerCameraAccess>(manager), store_.get());
        IpcServer* ipcPtr = ipc.get();
        manager->addFrameSink(
            [ipcPtr](const camera::Frame& frame) { ipcPtr->preview().submit(frame); });
        manager->setOnListChanged([ipcPtr] { ipcPtr->notifyCamerasChanged(); });
        ASSERT_TRUE(registry_.add(std::move(ipc)).has_value());
        ASSERT_TRUE(registry_.add(std::make_unique<CameraModule>(manager)).has_value());
        ModuleContext context{bus_};
        ASSERT_TRUE(registry_.startAll(context).has_value());

        dealer_ = zmq::socket_t{zctx_, zmq::socket_type::dealer};
        dealer_.set(zmq::sockopt::linger, 0);
        dealer_.connect("tcp://127.0.0.1:" + std::to_string(kCmdPort));
        sub_ = zmq::socket_t{zctx_, zmq::socket_type::sub};
        sub_.set(zmq::sockopt::linger, 0);
        sub_.set(zmq::sockopt::subscribe, "");
        sub_.connect("tcp://127.0.0.1:" + std::to_string(kEvtPort));
        ASSERT_TRUE(waitTopic(sub_, "hb", 5s).has_value()) << "no heartbeat";
    }
    void TearDown() override {
        dealer_.close();
        sub_.close();
        registry_.stopAll();
        std::filesystem::remove_all(dir_);
    }

    std::optional<std::vector<std::uint8_t>> roundTrip(const std::vector<std::uint8_t>& req) {
        (void)dealer_.send(zmq::buffer(req), zmq::send_flags::none);
        zmq::pollitem_t item{dealer_.handle(), 0, ZMQ_POLLIN, 0};
        zmq::poll(&item, 1, 3s);
        if ((item.revents & ZMQ_POLLIN) == 0) {
            return std::nullopt;
        }
        zmq::message_t reply;
        if (!dealer_.recv(reply)) {
            return std::nullopt;
        }
        const auto* d = static_cast<const std::uint8_t*>(reply.data());
        return std::vector<std::uint8_t>(d, d + reply.size());
    }

    // (id, state) of every camera in the GetCameraList reply.
    std::vector<std::pair<std::uint16_t, fb::CameraState>> cameraStates() {
        flatbuffers::FlatBufferBuilder fbb;
        const auto payload = fb::CreateGetCameraListRequest(fbb);
        const auto reply = roundTrip(request(fbb, fb::MsgType::GetCameraList, 1,
                                             fb::Payload::GetCameraListRequest, payload.Union()));
        std::vector<std::pair<std::uint16_t, fb::CameraState>> out;
        if (!reply) {
            return out;
        }
        const auto env = ipc::parseEnvelope(*reply);
        if (!env || (*env)->payload_type() != fb::Payload::CameraListReply) {
            return out;
        }
        if (const auto* list = (*env)->payload_as_CameraListReply()->cameras(); list != nullptr) {
            for (const auto* entry : *list) {
                out.emplace_back(entry->id(), entry->state());
            }
        }
        return out;
    }

    bool allStreaming(std::size_t count) {
        const auto states = cameraStates();
        return states.size() == count && std::ranges::all_of(states, [](const auto& s) {
                   return s.second == fb::CameraState::Streaming;
               });
    }

    std::filesystem::path dir_;
    testutil::FakeWorld world_;
    MessageBus bus_;
    std::unique_ptr<FileConfigStore> store_;
    ModuleRegistry registry_;
    zmq::context_t zctx_{1};
    zmq::socket_t dealer_;
    zmq::socket_t sub_;
};

} // namespace

TEST_F(CameraWiringTest, CameraListOverIpcShowsTheStreamingCameras) {
    world_.plug("SN-B");
    ASSERT_TRUE(waitFor([&] { return allStreaming(2); }));
    const auto states = cameraStates();
    EXPECT_EQ(states[0].first, 0U);
    EXPECT_EQ(states[1].first, 1U);
}

TEST_F(CameraWiringTest, PreviewFramesReachTheSharedMemoryRing) {
    ASSERT_TRUE(waitFor([&] { return cameraStates().size() == 2; }));
    ASSERT_TRUE(waitFor([&] {
        const auto states = cameraStates();
        return !states.empty() && states[0].second == fb::CameraState::Streaming;
    }));

    flatbuffers::FlatBufferBuilder fbb;
    const auto payload = fb::CreateSetPreviewRequest(fbb, 0, true, 30, 64);
    ASSERT_TRUE(roundTrip(request(fbb, fb::MsgType::SetPreview, 2, fb::Payload::SetPreviewRequest,
                                  payload.Union()))
                    .has_value());
    const auto event = waitTopic(sub_, "preview", 5s);
    ASSERT_TRUE(event.has_value());
    const auto* changed = event->env->payload_as_PreviewStreamChangedEvent();
    ASSERT_NE(changed, nullptr);
    ASSERT_NE(changed->shm_name(), nullptr);

    const auto reader = ipc::PreviewRingReader::open(changed->shm_name()->str());
    ASSERT_TRUE(reader.has_value());
    std::uint64_t seen = 0;
    std::optional<ipc::PreviewFrame> frame;
    ASSERT_TRUE(waitFor([&] {
        frame = (*reader)->readNewest(seen);
        return frame.has_value();
    }));
    EXPECT_EQ(frame->info.width, 64U);
    EXPECT_EQ(frame->info.height, 48U);
}

TEST_F(CameraWiringTest, CameraListChangedEventIsPublishedWhenACameraAppears) {
    ASSERT_TRUE(waitFor([&] {
        const auto states = cameraStates();
        return !states.empty() && states[0].second == fb::CameraState::Streaming;
    }));
    const auto drainUntil = std::chrono::steady_clock::now() + 500ms; // events of the first camera
    while (std::chrono::steady_clock::now() < drainUntil) {
        (void)recvEvent(sub_, 50ms);
    }

    world_.plug("SN-B");
    const auto event = waitTopic(sub_, "cam", 5s);
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->env->msg_type(), static_cast<std::uint16_t>(fb::MsgType::CameraListChanged));
    EXPECT_EQ(event->env->payload_type(), fb::Payload::CameraListChangedEvent);
}

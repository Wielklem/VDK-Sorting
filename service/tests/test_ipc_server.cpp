#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <zmq.hpp>

#include <vsort/common/message_bus.hpp>

#include "ipc/ipc_server.hpp"
#include "test_ipc_util.hpp"

using namespace vsort;
using namespace vsort::service;
using namespace std::chrono_literals;
using vsort::testutil::request;
namespace fb = vsort::ipc::fb;

namespace {

// Unusual ports so a running service on the default ones does not disturb the tests.
constexpr std::uint16_t kCmdPort = 47555;
constexpr std::uint16_t kEvtPort = 47556;

IpcConfig testConfig(std::uint16_t cmd = kCmdPort, std::uint16_t evt = kEvtPort) {
    IpcConfig c;
    c.commandPort = cmd;
    c.eventPort = evt;
    c.heartbeatInterval = 200ms;
    return c;
}

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

// Waits for an event on `topic`, skipping others.
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

class IpcServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        server = std::make_unique<IpcServer>(testConfig(),
                                             std::make_unique<testutil::FakeCameras>(), nullptr);
        ModuleContext ctx{bus};
        ASSERT_TRUE(server->init(ctx).has_value());
        ASSERT_TRUE(server->start().has_value());

        dealer = zmq::socket_t{zctx, zmq::socket_type::dealer};
        dealer.set(zmq::sockopt::linger, 0);
        dealer.connect("tcp://127.0.0.1:" + std::to_string(kCmdPort));

        sub = zmq::socket_t{zctx, zmq::socket_type::sub};
        sub.set(zmq::sockopt::linger, 0);
        sub.set(zmq::sockopt::subscribe, "");
        sub.connect("tcp://127.0.0.1:" + std::to_string(kEvtPort));
        // The first heartbeat proves the subscription is active.
        ASSERT_TRUE(waitTopic(sub, "hb", 5s).has_value()) << "no heartbeat";
    }
    void TearDown() override {
        dealer.close();
        sub.close();
        server->stop();
    }

    std::optional<std::vector<std::uint8_t>> roundTrip(const std::vector<std::uint8_t>& req) {
        (void)dealer.send(zmq::buffer(req), zmq::send_flags::none);
        zmq::pollitem_t item{dealer.handle(), 0, ZMQ_POLLIN, 0};
        zmq::poll(&item, 1, 3s);
        if ((item.revents & ZMQ_POLLIN) == 0) {
            return std::nullopt;
        }
        zmq::message_t reply;
        if (!dealer.recv(reply)) {
            return std::nullopt;
        }
        const auto* d = static_cast<const std::uint8_t*>(reply.data());
        return std::vector<std::uint8_t>(d, d + reply.size());
    }

    MessageBus bus;
    zmq::context_t zctx{1};
    std::unique_ptr<IpcServer> server;
    zmq::socket_t dealer;
    zmq::socket_t sub;
};

} // namespace

TEST_F(IpcServerTest, AnswersHelloOverZeroMq) {
    flatbuffers::FlatBufferBuilder fbb;
    const auto p = fb::CreateHelloRequest(fbb, ipc::kProtocolVersion);
    const auto reply =
        roundTrip(request(fbb, fb::MsgType::Hello, 77, fb::Payload::HelloRequest, p.Union()));
    ASSERT_TRUE(reply.has_value()) << "no reply within 3 s";
    const auto env = ipc::parseEnvelope(*reply);
    ASSERT_TRUE(env.has_value());
    EXPECT_EQ((*env)->request_id(), 77U);
    EXPECT_EQ((*env)->status(), 0U);
    EXPECT_NE((*env)->payload_as_HelloReply(), nullptr);
    EXPECT_EQ(server->health().state, HealthState::Ok);
}

TEST_F(IpcServerTest, GarbageDoesNotKillTheServer) {
    const auto reply = roundTrip({9, 9, 9, 9, 9, 9, 9, 9});
    ASSERT_TRUE(reply.has_value());
    const auto env = ipc::parseEnvelope(*reply);
    ASSERT_TRUE(env.has_value());
    EXPECT_EQ((*env)->status(), static_cast<std::uint16_t>(Errc::ParseError));

    flatbuffers::FlatBufferBuilder fbb;
    const auto p = fb::CreateGetCameraListRequest(fbb);
    EXPECT_TRUE(roundTrip(request(fbb, fb::MsgType::GetCameraList, 1,
                                  fb::Payload::GetCameraListRequest, p.Union()))
                    .has_value());
}

TEST_F(IpcServerTest, HeartbeatsCountUp) {
    const auto a = waitTopic(sub, "hb", 3s);
    const auto b = waitTopic(sub, "hb", 3s);
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a->env->msg_type(), static_cast<std::uint16_t>(fb::MsgType::Heartbeat));
    EXPECT_LT(a->env->payload_as_HeartbeatEvent()->seq(),
              b->env->payload_as_HeartbeatEvent()->seq());
}

TEST_F(IpcServerTest, ConfigChangesAreForwardedAsEvents) {
    ConfigVersion v;
    v.number = 3;
    bus.publish(ConfigChanged{"camera_map", v});
    const auto e = waitTopic(sub, "cfg", 3s);
    ASSERT_TRUE(e.has_value());
    const auto* ev = e->env->payload_as_ConfigChangedEvent();
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->module_name()->str(), "camera_map");
    EXPECT_EQ(ev->version(), 3U);
}

TEST_F(IpcServerTest, PreviewRingEventAndFramesReachTheHmiSide) {
    VSORT_SKIP_IF_NO_SHM();
    flatbuffers::FlatBufferBuilder fbb;
    const auto p = fb::CreateSetPreviewRequest(fbb, 1, true, 60, 64);
    const auto reply = roundTrip(
        request(fbb, fb::MsgType::SetPreview, 1, fb::Payload::SetPreviewRequest, p.Union()));
    ASSERT_TRUE(reply.has_value());
    ASSERT_EQ((*ipc::parseEnvelope(*reply))->status(), 0U);

    const auto frame = vsort::testutil::makeFrame(1, 5, 128, 64, 33);
    server->preview().submit(frame->frame);
    const auto e = waitTopic(sub, "preview", 3s);
    ASSERT_TRUE(e.has_value());
    const auto* ev = e->env->payload_as_PreviewStreamChangedEvent();
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->camera_id(), 1U);
    EXPECT_EQ(ev->width(), 64U);
    EXPECT_EQ(ev->height(), 32U);
    EXPECT_EQ(ev->pixel_format(), fb::PixelFormat::Mono8);

    auto reader = ipc::PreviewRingReader::open(ev->shm_name()->str());
    ASSERT_TRUE(reader.has_value()) << reader.error().what();
    std::uint64_t seen = 0;
    for (int i = 0; i < 100 && !(*reader)->readNewest(seen).has_value(); ++i) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_GT(seen, 0U);
}

TEST(IpcServer, StartFailsWhenPortIsTaken) {
    MessageBus bus;
    ModuleContext ctx{bus};
    IpcServer first{testConfig(47565, 47566), nullptr, nullptr};
    ASSERT_TRUE(first.init(ctx).has_value());
    ASSERT_TRUE(first.start().has_value());

    IpcServer second{testConfig(47565, 47566), nullptr, nullptr};
    ASSERT_TRUE(second.init(ctx).has_value());
    const auto started = second.start();
    ASSERT_FALSE(started.has_value());
    EXPECT_EQ(started.error().code, Errc::IoError);
    second.stop(); // safe after a failed start
    first.stop();
    first.stop(); // idempotent
}

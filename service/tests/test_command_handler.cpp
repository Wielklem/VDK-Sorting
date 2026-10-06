#include <filesystem>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <vsort/common/message_bus.hpp>

#include "ipc/command_handler.hpp"
#include "test_ipc_util.hpp"

using namespace vsort;
using namespace vsort::service;
using vsort::testutil::request;
namespace fb = vsort::ipc::fb;

namespace {

using Bytes = std::vector<std::uint8_t>;

struct Reply {
    Bytes bytes;
    const fb::Envelope* env{nullptr};
};

Reply parse(Bytes bytes) {
    Reply r;
    r.bytes = std::move(bytes);
    const auto env = ipc::parseEnvelope(r.bytes);
    EXPECT_TRUE(env.has_value());
    r.env = env.value_or(nullptr);
    return r;
}

std::string status(const Reply& r) {
    return std::string{toString(static_cast<Errc>(r.env->status()))};
}

class CommandHandlerTest : public ::testing::Test {
protected:
    PreviewHub hub{PreviewDefaults{}};
    testutil::FakeCameras cameras;
    CommandHandler handler{hub, cameras, nullptr, "9.9.9"};

    Reply call(const Bytes& req) { return parse(handler.handle(req)); }

    Reply hello() {
        flatbuffers::FlatBufferBuilder fbb;
        const auto p = fb::CreateHelloRequest(fbb, ipc::kProtocolVersion);
        return call(request(fbb, fb::MsgType::Hello, 11, fb::Payload::HelloRequest, p.Union()));
    }
};

} // namespace

TEST_F(CommandHandlerTest, HelloReportsVersionStateAndEchoesRequestId) {
    const auto r = hello();
    EXPECT_EQ(r.env->status(), 0U);
    EXPECT_EQ(r.env->request_id(), 11U);
    EXPECT_EQ(r.env->msg_type(), static_cast<std::uint16_t>(fb::MsgType::Hello));
    EXPECT_EQ(r.env->protocol_version(), ipc::kProtocolVersion);
    const auto* reply = r.env->payload_as_HelloReply();
    ASSERT_NE(reply, nullptr);
    EXPECT_EQ(reply->service_version()->str(), "9.9.9");
    EXPECT_EQ(reply->service_state(), fb::ServiceState::Running);
    ASSERT_NE(reply->capabilities(), nullptr);
    EXPECT_GE(reply->capabilities()->size(), 1U);
}

TEST_F(CommandHandlerTest, GarbageGivesParseErrorReply) {
    const auto r = call(Bytes{1, 2, 3, 4, 5, 6, 7, 8});
    EXPECT_EQ(r.env->status(), static_cast<std::uint16_t>(Errc::ParseError));
    EXPECT_EQ(r.env->request_id(), 0U);
    ASSERT_NE(r.env->error_text(), nullptr);
}

TEST_F(CommandHandlerTest, EventPayloadAsRequestIsNotSupported) {
    flatbuffers::FlatBufferBuilder fbb;
    const auto p = fb::CreateHeartbeatEvent(fbb, fb::ServiceState::Running, 1, 1);
    const auto r =
        call(request(fbb, fb::MsgType::Hello, 5, fb::Payload::HeartbeatEvent, p.Union()));
    EXPECT_EQ(r.env->status(), static_cast<std::uint16_t>(Errc::NotSupported));
    EXPECT_EQ(r.env->request_id(), 5U);
}

TEST_F(CommandHandlerTest, CameraListShowsCamerasAndPreviewState) {
    ASSERT_TRUE(hub.setPreview(2, {.enabled = true, .fps = 5, .maxWidth = 320}).has_value());
    flatbuffers::FlatBufferBuilder fbb;
    const auto p = fb::CreateGetCameraListRequest(fbb);
    const auto r = call(
        request(fbb, fb::MsgType::GetCameraList, 1, fb::Payload::GetCameraListRequest, p.Union()));
    ASSERT_EQ(r.env->status(), 0U);
    const auto* list = r.env->payload_as_CameraListReply();
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->cameras()->size(), 2U);
    const auto* a = list->cameras()->Get(0);
    EXPECT_EQ(a->id(), 1U);
    EXPECT_EQ(a->serial()->str(), "SN-A");
    EXPECT_EQ(a->state(), fb::CameraState::Streaming);
    EXPECT_FALSE(a->preview_enabled());
    const auto* b = list->cameras()->Get(1);
    EXPECT_TRUE(b->preview_enabled());
    EXPECT_EQ(b->preview_fps(), 5U);
    EXPECT_EQ(b->preview_max_width(), 320U);
}

TEST_F(CommandHandlerTest, SetPreviewAppliesAndZeroKeepsCurrentValues) {
    auto send = [&](std::uint16_t cam, bool enabled, std::uint16_t fps, std::uint32_t width) {
        flatbuffers::FlatBufferBuilder fbb;
        const auto p = fb::CreateSetPreviewRequest(fbb, cam, enabled, fps, width);
        return call(
            request(fbb, fb::MsgType::SetPreview, 3, fb::Payload::SetPreviewRequest, p.Union()));
    };
    auto r = send(1, true, 20, 480);
    ASSERT_EQ(r.env->status(), 0U) << status(r);
    EXPECT_TRUE(r.env->payload_as_SetPreviewReply()->camera()->preview_enabled());
    EXPECT_EQ(hub.settings(1).fps, 20U);
    EXPECT_EQ(hub.settings(1).maxWidth, 480U);

    r = send(1, true, 0, 0); // keep fps and width
    ASSERT_EQ(r.env->status(), 0U);
    EXPECT_EQ(hub.settings(1).fps, 20U);
    EXPECT_EQ(hub.settings(1).maxWidth, 480U);

    r = send(1, false, 0, 0);
    ASSERT_EQ(r.env->status(), 0U);
    EXPECT_FALSE(hub.settings(1).enabled);
}

TEST_F(CommandHandlerTest, SetPreviewRejectsUnknownCameraAndBadValues) {
    auto send = [&](std::uint16_t cam, std::uint16_t fps, std::uint32_t width) {
        flatbuffers::FlatBufferBuilder fbb;
        const auto p = fb::CreateSetPreviewRequest(fbb, cam, true, fps, width);
        return call(
            request(fbb, fb::MsgType::SetPreview, 3, fb::Payload::SetPreviewRequest, p.Union()));
    };
    EXPECT_EQ(send(99, 10, 640).env->status(), static_cast<std::uint16_t>(Errc::NotFound));
    EXPECT_EQ(send(1, 100, 640).env->status(), static_cast<std::uint16_t>(Errc::InvalidArgument));
    EXPECT_EQ(send(1, 10, 5).env->status(), static_cast<std::uint16_t>(Errc::InvalidArgument));
    EXPECT_FALSE(hub.settings(1).enabled);
}

TEST_F(CommandHandlerTest, CameraSettingsRoundTrip) {
    {
        flatbuffers::FlatBufferBuilder fbb;
        const auto s = fb::CreateCameraSettings(fbb, 2500.0, 3.5, 10, 20, 640, 480,
                                                fb::TriggerMode::Software, false);
        const auto p = fb::CreateSetCameraSettingsRequest(fbb, 2, s);
        const auto r = call(request(fbb, fb::MsgType::SetCameraSettings, 4,
                                    fb::Payload::SetCameraSettingsRequest, p.Union()));
        ASSERT_EQ(r.env->status(), 0U) << status(r);
        EXPECT_EQ(r.env->payload_type(), fb::Payload::NONE);
    }
    flatbuffers::FlatBufferBuilder fbb;
    const auto p = fb::CreateGetCameraSettingsRequest(fbb, 2);
    const auto r = call(request(fbb, fb::MsgType::GetCameraSettings, 5,
                                fb::Payload::GetCameraSettingsRequest, p.Union()));
    ASSERT_EQ(r.env->status(), 0U);
    const auto* s = r.env->payload_as_CameraSettingsReply()->settings();
    EXPECT_DOUBLE_EQ(s->exposure_us(), 2500.0);
    EXPECT_DOUBLE_EQ(s->gain_db(), 3.5);
    EXPECT_EQ(s->roi_width(), 640U);
    EXPECT_EQ(s->trigger_mode(), fb::TriggerMode::Software);
    EXPECT_FALSE(s->trigger_rising());
}

TEST_F(CommandHandlerTest, CameraErrorsArePassedThrough) {
    flatbuffers::FlatBufferBuilder fbb;
    const auto s =
        fb::CreateCameraSettings(fbb, 0.0, 0.0, 0, 0, 0, 0, fb::TriggerMode::Hardware, true);
    const auto p = fb::CreateSetCameraSettingsRequest(fbb, 1, s);
    const auto bad = call(request(fbb, fb::MsgType::SetCameraSettings, 6,
                                  fb::Payload::SetCameraSettingsRequest, p.Union()));
    EXPECT_EQ(bad.env->status(), static_cast<std::uint16_t>(Errc::InvalidArgument));
    ASSERT_NE(bad.env->error_text(), nullptr);

    flatbuffers::FlatBufferBuilder fbb2;
    const auto g = fb::CreateGetCameraSettingsRequest(fbb2, 77);
    const auto missing = call(request(fbb2, fb::MsgType::GetCameraSettings, 7,
                                      fb::Payload::GetCameraSettingsRequest, g.Union()));
    EXPECT_EQ(missing.env->status(), static_cast<std::uint16_t>(Errc::NotFound));
}

TEST_F(CommandHandlerTest, SettingsRequestWithoutSettingsIsInvalid) {
    flatbuffers::FlatBufferBuilder fbb;
    const auto p = fb::CreateSetCameraSettingsRequest(fbb, 1, 0);
    const auto r = call(request(fbb, fb::MsgType::SetCameraSettings, 8,
                                fb::Payload::SetCameraSettingsRequest, p.Union()));
    EXPECT_EQ(r.env->status(), static_cast<std::uint16_t>(Errc::InvalidArgument));
}

TEST_F(CommandHandlerTest, ConfigWithoutStoreIsNotSupported) {
    flatbuffers::FlatBufferBuilder fbb;
    const auto p = fb::CreateGetConfigRequest(fbb, fbb.CreateString("x"));
    const auto r =
        call(request(fbb, fb::MsgType::GetConfig, 9, fb::Payload::GetConfigRequest, p.Union()));
    EXPECT_EQ(r.env->status(), static_cast<std::uint16_t>(Errc::NotSupported));
}

class CommandHandlerConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("vsort_ipc_cfg_" + std::to_string(Timestamp::now().ns()));
        std::filesystem::create_directories(dir_);
        store_ = std::make_unique<FileConfigStore>(dir_, bus_);
        const nlohmann::json schema = {
            {"type", "object"},
            {"properties", {{"level", {{"type", "integer"}, {"minimum", 0}, {"maximum", 9}}}}},
            {"required", {"level"}}};
        ASSERT_TRUE(store_->registerModule("ipc_test", schema, {{"level", 1}}).has_value());
        handler_ = std::make_unique<CommandHandler>(hub_, cameras_, store_.get(), "1");
    }
    void TearDown() override {
        handler_.reset();
        store_.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    Reply get(const std::string& module) {
        flatbuffers::FlatBufferBuilder fbb;
        const auto p = fb::CreateGetConfigRequest(fbb, fbb.CreateString(module));
        return parse(handler_->handle(
            request(fbb, fb::MsgType::GetConfig, 1, fb::Payload::GetConfigRequest, p.Union())));
    }
    Reply set(const std::string& module, const std::string& json) {
        flatbuffers::FlatBufferBuilder fbb;
        const auto p =
            fb::CreateSetConfigRequest(fbb, fbb.CreateString(module), fbb.CreateString(json), 0);
        return parse(handler_->handle(
            request(fbb, fb::MsgType::SetConfig, 2, fb::Payload::SetConfigRequest, p.Union())));
    }

    MessageBus bus_;
    PreviewHub hub_{PreviewDefaults{}};
    testutil::FakeCameras cameras_;
    std::filesystem::path dir_;
    std::unique_ptr<FileConfigStore> store_;
    std::unique_ptr<CommandHandler> handler_;
};

TEST_F(CommandHandlerConfigTest, GetAndSetConfig) {
    auto r = get("ipc_test");
    ASSERT_EQ(r.env->status(), 0U) << status(r);
    EXPECT_EQ(r.env->payload_as_ConfigReply()->version(), 1U);
    EXPECT_EQ(nlohmann::json::parse(r.env->payload_as_ConfigReply()->json()->str())["level"], 1);

    r = set("ipc_test", R"({"level": 5})");
    ASSERT_EQ(r.env->status(), 0U) << status(r);
    EXPECT_EQ(r.env->payload_as_ConfigReply()->version(), 2U);
    EXPECT_EQ(nlohmann::json::parse(
                  get("ipc_test").env->payload_as_ConfigReply()->json()->str())["level"],
              5);
}

TEST_F(CommandHandlerConfigTest, SetConfigErrors) {
    EXPECT_EQ(set("ipc_test", "{not json").env->status(),
              static_cast<std::uint16_t>(Errc::ParseError));
    EXPECT_EQ(set("ipc_test", R"({"level": 99})").env->status(),
              static_cast<std::uint16_t>(Errc::ValidationFailed));
    EXPECT_EQ(set("nope", "{}").env->status(), static_cast<std::uint16_t>(Errc::NotFound));
    EXPECT_EQ(get("nope").env->status(), static_cast<std::uint16_t>(Errc::NotFound));
    EXPECT_EQ(get("ipc_test").env->payload_as_ConfigReply()->version(), 1U); // nothing was saved
}

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <vsort/ipc/envelope.hpp>

using namespace vsort;
using namespace vsort::ipc;

TEST(Envelope, RoundTripWithPayload) {
    flatbuffers::FlatBufferBuilder fbb;
    const auto hb = fb::CreateHeartbeatEvent(fbb, fb::ServiceState::Running, 1234, 7);
    const auto bytes = finishEnvelope(fbb,
                                      {.type = fb::MsgType::Heartbeat,
                                       .requestId = 0,
                                       .timestampNs = 99,
                                       .status = 0,
                                       .errorText = {}},
                                      fb::Payload::HeartbeatEvent, hb.Union());

    const auto env = parseEnvelope(bytes);
    ASSERT_TRUE(env.has_value()) << env.error().what();
    EXPECT_EQ((*env)->protocol_version(), kProtocolVersion);
    EXPECT_EQ((*env)->msg_type(), static_cast<std::uint16_t>(fb::MsgType::Heartbeat));
    EXPECT_EQ((*env)->timestamp_ns(), 99U);
    ASSERT_EQ((*env)->payload_type(), fb::Payload::HeartbeatEvent);
    const auto* event = (*env)->payload_as_HeartbeatEvent();
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event->seq(), 7U);
    EXPECT_EQ(event->uptime_ms(), 1234U);
}

TEST(Envelope, ErrorReplyCarriesStatusAndText) {
    flatbuffers::FlatBufferBuilder fbb;
    const auto bytes = finishEnvelope(fbb,
                                      {.type = fb::MsgType::GetConfig,
                                       .requestId = 42,
                                       .timestampNs = 1,
                                       .status = static_cast<std::uint16_t>(Errc::NotFound),
                                       .errorText = "no such module: ünïcode"},
                                      fb::Payload::NONE, {});
    const auto env = parseEnvelope(bytes);
    ASSERT_TRUE(env.has_value());
    EXPECT_EQ((*env)->request_id(), 42U);
    EXPECT_EQ((*env)->status(), static_cast<std::uint16_t>(Errc::NotFound));
    ASSERT_NE((*env)->error_text(), nullptr);
    EXPECT_EQ((*env)->error_text()->str(), "no such module: ünïcode");
    EXPECT_EQ((*env)->payload_type(), fb::Payload::NONE);
}

TEST(Envelope, RejectsGarbageAndEmpty) {
    const std::vector<std::uint8_t> garbage{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    const auto bad = parseEnvelope(garbage);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, Errc::ParseError);

    const auto empty = parseEnvelope({});
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().code, Errc::ParseError);
}

TEST(Envelope, RejectsTruncatedBuffer) {
    flatbuffers::FlatBufferBuilder fbb;
    const auto hello = fb::CreateHelloRequest(fbb, 1);
    auto bytes = finishEnvelope(fbb, {.type = fb::MsgType::Hello, .errorText = {}},
                                fb::Payload::HelloRequest, hello.Union());
    bytes.resize(bytes.size() / 2);
    EXPECT_FALSE(parseEnvelope(bytes).has_value());
}

TEST(Envelope, RejectsMisalignedBuffer) {
    flatbuffers::FlatBufferBuilder fbb;
    const auto hello = fb::CreateHelloRequest(fbb, 1);
    const auto bytes = finishEnvelope(fbb, {.type = fb::MsgType::Hello, .errorText = {}},
                                      fb::Payload::HelloRequest, hello.Union());
    std::vector<std::uint8_t> shifted(bytes.size() + 8);
    std::copy(bytes.begin(), bytes.end(), shifted.begin() + 1);
    const auto r = parseEnvelope(std::span<const std::uint8_t>{shifted}.subspan(1, bytes.size()));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Errc::InvalidArgument);
}

#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include <vsort/common/error.hpp>
#include <vsort/ipc/vdk_ipc_generated.h>

namespace vsort::ipc {

// Bumped on a breaking change. The HMI compares it with Envelope::protocol_version.
inline constexpr std::uint16_t kProtocolVersion = 1;

// Default loopback endpoints (see docs/ipc-design.md).
inline constexpr std::uint16_t kDefaultCommandPort = 5555;
inline constexpr std::uint16_t kDefaultEventPort = 5556;

// Topic (first frame) of every event on the PUB socket.
inline constexpr std::string_view kTopicHeartbeat = "hb";
inline constexpr std::string_view kTopicConfig = "cfg";
inline constexpr std::string_view kTopicPreview = "preview";

struct EnvelopeFields {
    fb::MsgType type{fb::MsgType::Hello};
    std::uint64_t requestId{0};
    std::uint64_t timestampNs{0};
    std::uint16_t status{0}; // 0 = ok, else static_cast<uint16_t>(Errc)
    std::string_view errorText;
};

// Wraps an already built payload into an Envelope and finishes the buffer.
// `payload` must have been created with the same builder; use NONE/0 for "no payload".
[[nodiscard]] std::vector<std::uint8_t> finishEnvelope(flatbuffers::FlatBufferBuilder& builder,
                                                       const EnvelopeFields& fields,
                                                       fb::Payload payloadType,
                                                       flatbuffers::Offset<void> payload);

// Verifies the buffer and returns the root. The pointer is valid as long as `bytes` is.
// Errc::ParseError for anything that is not a valid Envelope. Errc::InvalidArgument when the
// buffer is not 8-byte aligned (always copy ZeroMQ messages into a std::vector first).
[[nodiscard]] Result<const fb::Envelope*> parseEnvelope(std::span<const std::uint8_t> bytes);

} // namespace vsort::ipc

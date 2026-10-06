#include <cstdint>

#include <vsort/ipc/envelope.hpp>

namespace vsort::ipc {

std::vector<std::uint8_t> finishEnvelope(flatbuffers::FlatBufferBuilder& builder,
                                         const EnvelopeFields& fields, fb::Payload payloadType,
                                         flatbuffers::Offset<void> payload) {
    const auto error = fields.errorText.empty()
                           ? flatbuffers::Offset<flatbuffers::String>{}
                           : builder.CreateString(fields.errorText.data(), fields.errorText.size());
    const auto envelope = fb::CreateEnvelope(
        builder, kProtocolVersion, static_cast<std::uint16_t>(fields.type), fields.requestId,
        fields.timestampNs, fields.status, error, payloadType, payload);
    fb::FinishEnvelopeBuffer(builder, envelope);
    const std::uint8_t* data = builder.GetBufferPointer();
    return {data, data + builder.GetSize()};
}

Result<const fb::Envelope*> parseEnvelope(std::span<const std::uint8_t> bytes) {
    // FlatBuffers reads 8-byte scalars in place. ZeroMQ message buffers are NOT aligned:
    // copy them into a std::vector first.
    if (reinterpret_cast<std::uintptr_t>(bytes.data()) % alignof(std::uint64_t) != 0) {
        return makeError(Errc::InvalidArgument, "IPC buffer is not 8-byte aligned");
    }
    flatbuffers::Verifier verifier{bytes.data(), bytes.size()};
    if (!fb::VerifyEnvelopeBuffer(verifier)) {
        return makeError(Errc::ParseError, "not a valid IPC envelope");
    }
    return fb::GetEnvelope(bytes.data());
}

} // namespace vsort::ipc

#pragma once

#include <cstdint>
#include <vector>

#include <vsort/ipc/envelope.hpp>

#include "cups/cup_types.hpp"

namespace vsort::service {

// FlatBuffers form of the Product Monitor messages (MSG-50-02, P80.100).
[[nodiscard]] flatbuffers::Offset<ipc::fb::Cup> makeCup(flatbuffers::FlatBufferBuilder& fbb,
                                                        const CupState& cup);

[[nodiscard]] flatbuffers::Offset<ipc::fb::CupSnapshotReply>
makeCupSnapshot(flatbuffers::FlatBufferBuilder& fbb, const std::vector<LaneSnapshot>& lanes);

// Complete event envelope for the "cups" topic.
[[nodiscard]] std::vector<std::uint8_t> makeCupUpdateEnvelope(const CupUpdate& update);

} // namespace vsort::service

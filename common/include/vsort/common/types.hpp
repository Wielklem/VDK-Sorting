#pragma once

#include <cstdint>

#include <vsort/common/strong_id.hpp>

namespace vsort {

using FrameId  = StrongId<struct FrameIdTag, std::uint64_t>;
using ObjectId = StrongId<struct ObjectIdTag, std::uint64_t>;

} // namespace vsort

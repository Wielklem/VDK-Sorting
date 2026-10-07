#pragma once

#include <cstdint>

#include <vsort/camera/camera.hpp>
#include <vsort/common/timestamp.hpp>
#include <vsort/common/types.hpp>

namespace vsort::service {

enum class PhotoStatus : std::uint8_t { Ok = 1, NoData = 2 };

// MSG-50-01 (V1, P40.10): one sensor's observation of one cup. The records of all sensors of a
// lane with the same cupId together describe one object (assembled in P80.100). Frame fields are
// only set for Ok; pixels are not carried (see architecture section 40.40).
// A later record for the same lane, cup and sensor REPLACES the earlier one (P40.40 corrections).
// [REDO] P130 adds the encoder position. IPC form: ipc::fb::ObjectRecord.
struct ObjectRecord {
    std::uint16_t laneId{0};
    std::int64_t cupId{0}; // sensorCount - offset; < 0: the cup passed sensor 1 before start
    std::uint16_t sensorId{0};
    std::uint64_t sensorCount{0}; // cups counted at this sensor since tracking start
    PhotoStatus status{PhotoStatus::NoData};
    std::uint16_t cameraId{0};
    FrameId frameId;
    Timestamp hostTimestamp;
    std::uint64_t deviceTimestampNs{0}; // camera clock, 0 if unavailable
    camera::Roi crop;                   // lane crop in frame pixels
};

} // namespace vsort::service

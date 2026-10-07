#pragma once

#include <cstdint>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/common/timestamp.hpp>

#include "tracking/object_record.hpp"

namespace vsort::service {

struct SensorCounters {
    std::uint16_t laneId{0};
    std::uint16_t sensorId{0};
    std::uint16_t cameraId{0};
    std::uint64_t count{0};    // cups counted (Ok + NoData)
    std::uint64_t noData{0};   // cups marked NoData
    std::uint64_t idResets{0}; // frame ID restarted (reconnect): misses unknown

    bool operator==(const SensorCounters&) const = default;
};

// M50 (P40.10): turns camera frames into ObjectRecords (MSG-50-01). V1: M50.10 frame sequence;
// later M50.20 encoder. Not thread-safe: one thread calls everything.
class ITracker {
public:
    ITracker() = default;
    virtual ~ITracker() = default;
    ITracker(const ITracker&) = delete;
    ITracker& operator=(const ITracker&) = delete;
    ITracker(ITracker&&) = delete;
    ITracker& operator=(ITracker&&) = delete;

    // Every frame of every camera, in arrival order per camera. Appends the resulting records.
    virtual void onFrame(const camera::FrameMetadata& meta, std::vector<ObjectRecord>& out) = 0;

    // Called regularly (about 10 Hz) for time-based decisions (P40.40). May append records.
    virtual void onTick(Timestamp now, std::vector<ObjectRecord>& out) = 0;

    [[nodiscard]] virtual std::vector<SensorCounters> counters() const = 0;
};

} // namespace vsort::service

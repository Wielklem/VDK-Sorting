#pragma once

#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include <vsort/camera/camera.hpp>
#include <vsort/common/timestamp.hpp>

#include "tracking/object_record.hpp"

namespace vsort::service {

// Per camera, detected from frame timing (P40.40). No signal from the machine in V1.
enum class CameraPhase : std::uint8_t { Silent = 0, Starting, Running };

struct SensorCounters {
    std::uint16_t laneId{0};
    std::uint16_t sensorId{0};
    std::uint16_t cameraId{0};
    std::uint64_t count{0};        // cups counted (Ok + NoData)
    std::uint64_t noData{0};       // NoData records (gaps, timing misses, repairs)
    std::uint64_t idResets{0};     // frame ID restarted (reconnect): misses unknown
    std::uint64_t timingMisses{0}; // missed triggers found by timing
    std::uint64_t repairs{0};      // counter corrected by the cross-sensor check
    std::uint64_t extraFrames{0};  // frames dropped as double triggers
    CameraPhase phase{CameraPhase::Silent};

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

    // Learned state worth keeping across service restarts (JSON, may be empty).
    [[nodiscard]] virtual nlohmann::json state() const { return nlohmann::json::object(); }
    virtual void restoreState(const nlohmann::json& /*state*/) {}
};

} // namespace vsort::service

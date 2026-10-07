#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include <nlohmann/json.hpp>

#include <vsort/camera/health.hpp>
#include <vsort/common/machine_config.hpp>

#include "tracking/lane_crop.hpp"
#include "tracking/tracker.hpp"

namespace vsort::service {

// One sensor as the tracker sees it.
struct TrackedSensor {
    std::uint16_t laneId{0};
    std::uint16_t sensorId{0};
    std::uint16_t cameraId{0};
    std::uint32_t offsetCups{0};
    NormalizedRect roi; // whole image if the ROI is 0 or not found
};

// All sensors of the machine, config order. Missing ROIs fall back to the whole image
// (the service already warned at start).
[[nodiscard]] std::vector<TrackedSensor> makeTrackedSensors(const MachineConfig& machine,
                                                            const nlohmann::json& roiConfig);

struct FrameSequenceOptions {
    // A frame-ID gap up to this size is filled with NoData cups; a larger jump is handled like
    // a frame ID reset (the number of lost cups is not trusted).
    std::uint64_t maxGapFill{100};
};

// M50.10 (P40.30): one frame = one cup per camera (hardware trigger). Per sensor a cup counter;
// lane cup ID = counter - offset. Frame-ID gaps (frames lost after exposure) give NoData cups.
// Missed triggers (no frame at all) are not detected here: P40.40.
class FrameSequenceTracker final : public ITracker {
public:
    explicit FrameSequenceTracker(std::vector<TrackedSensor> sensors,
                                  FrameSequenceOptions options = {});

    void onFrame(const camera::FrameMetadata& meta, std::vector<ObjectRecord>& out) override;
    void onTick(Timestamp now, std::vector<ObjectRecord>& out) override;
    [[nodiscard]] std::vector<SensorCounters> counters() const override;

    [[nodiscard]] std::uint64_t unmappedFrames() const noexcept { return unmappedFrames_; }

private:
    struct SensorState {
        TrackedSensor config;
        std::uint64_t count{0};
        std::uint64_t noData{0};
        std::uint64_t idResets{0};
    };
    struct CameraState {
        camera::FrameIdTracker ids;
        std::vector<std::size_t> sensors; // indices into sensors_
    };

    ObjectRecord next(SensorState& sensor);

    FrameSequenceOptions options_;
    std::vector<SensorState> sensors_;
    std::map<std::uint16_t, CameraState> cameras_;
    std::uint64_t unmappedFrames_{0};
};

} // namespace vsort::service

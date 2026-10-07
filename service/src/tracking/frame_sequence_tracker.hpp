#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include <nlohmann/json.hpp>

#include <vsort/common/machine_config.hpp>

#include "tracking/camera_timing.hpp"
#include "tracking/lane_consistency.hpp"
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
    TimingOptions timing;
    ConsistencyOptions consistency;
    std::uint32_t markLimit{50}; // a repair marks at most this many earlier cups NoData
};

// M50.10: one frame = one cup per camera (hardware trigger, P40.30). Per sensor a cup counter;
// lane cup ID = counter - offset. NoData cups come from frame-ID gaps (exact), from the camera
// timing (P40.40 a-c) and from the cross-sensor check (P40.40 d), which also repairs counters.
// A later record for the same lane, cup and sensor replaces the earlier one.
class FrameSequenceTracker final : public ITracker {
public:
    explicit FrameSequenceTracker(std::vector<TrackedSensor> sensors,
                                  FrameSequenceOptions options = {});

    void onFrame(const camera::FrameMetadata& meta, std::vector<ObjectRecord>& out) override;
    void onTick(Timestamp now, std::vector<ObjectRecord>& out) override;
    [[nodiscard]] std::vector<SensorCounters> counters() const override;
    // {"lanes": [{"lane_id": 1, "sensors": [{"sensor_id": 1, "phase": 0.12}, ...]}]}
    [[nodiscard]] nlohmann::json state() const override;
    void restoreState(const nlohmann::json& state) override;

    [[nodiscard]] std::uint64_t unmappedFrames() const noexcept { return unmappedFrames_; }

private:
    struct SensorState {
        TrackedSensor config;
        std::size_t lane{0};       // index into lanes_
        std::size_t position{0};   // index in the lane (upstream first)
        std::int64_t count{0};     // index of the next cup
        std::int64_t lastGood{-1}; // last index the cross-sensor check agreed with
        std::int64_t lastIndex{0}; // index of the last frame
        Timestamp lastHost;        // host time of the last frame
        bool hasFrame{false};
        SensorCounters stats;
    };
    struct CameraState {
        CameraTiming timing;
        std::vector<std::size_t> sensors; // indices into sensors_
    };
    struct LaneState {
        std::uint16_t laneId{0};
        std::vector<std::size_t> sensors; // indices into sensors_, lane order
        LaneConsistency consistency;
    };

    void apply(const std::vector<FrameDecision>& decisions, std::vector<ObjectRecord>& out);
    void apply(SensorState& sensor, const FrameDecision& decision, std::vector<ObjectRecord>& out);
    [[nodiscard]] std::vector<SensorView> views(const LaneState& lane) const;
    [[nodiscard]] ObjectRecord record(const SensorState& sensor, std::int64_t index) const;
    void emitNoData(SensorState& sensor, std::int64_t from, std::int64_t to,
                    std::vector<ObjectRecord>& out);

    FrameSequenceOptions options_;
    std::vector<SensorState> sensors_;
    std::map<std::uint16_t, CameraState> cameras_;
    std::vector<LaneState> lanes_;
    std::uint64_t unmappedFrames_{0};
};

} // namespace vsort::service

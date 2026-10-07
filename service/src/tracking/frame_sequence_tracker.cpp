#include "tracking/frame_sequence_tracker.hpp"

#include <utility>

namespace vsort::service {

std::vector<TrackedSensor> makeTrackedSensors(const MachineConfig& machine,
                                              const nlohmann::json& roiConfig) {
    std::vector<TrackedSensor> out;
    for (const auto* lane : machine.lanes()) {
        for (const auto& s : lane->sensors) {
            out.push_back(
                {.laneId = lane->id,
                 .sensorId = s.id,
                 .cameraId = s.cameraId,
                 .offsetCups = s.offsetCups,
                 .roi = findRoi(roiConfig, s.cameraId, s.roiId).value_or(NormalizedRect{})});
        }
    }
    return out;
}

FrameSequenceTracker::FrameSequenceTracker(std::vector<TrackedSensor> sensors,
                                           FrameSequenceOptions options)
    : options_{options} {
    sensors_.reserve(sensors.size());
    for (auto& s : sensors) {
        cameras_[s.cameraId].sensors.push_back(sensors_.size());
        sensors_.push_back(SensorState{.config = std::move(s)});
    }
}

ObjectRecord FrameSequenceTracker::next(SensorState& sensor) {
    ObjectRecord r;
    r.laneId = sensor.config.laneId;
    r.sensorId = sensor.config.sensorId;
    r.cameraId = sensor.config.cameraId;
    r.sensorCount = sensor.count;
    r.cupId = static_cast<std::int64_t>(sensor.count) -
              static_cast<std::int64_t>(sensor.config.offsetCups);
    ++sensor.count;
    return r;
}

void FrameSequenceTracker::onFrame(const camera::FrameMetadata& meta,
                                   std::vector<ObjectRecord>& out) {
    const auto it = cameras_.find(meta.cameraIndex);
    if (it == cameras_.end()) {
        ++unmappedFrames_;
        return;
    }
    const auto seen = it->second.ids.observe(meta.frameId);
    std::uint64_t missed = 0;
    bool reset = seen.kind == camera::FrameIdTracker::Kind::Reset;
    if (seen.kind == camera::FrameIdTracker::Kind::Gap) {
        if (seen.missed <= options_.maxGapFill) {
            missed = seen.missed;
        } else {
            reset = true;
        }
    }
    for (const std::size_t index : it->second.sensors) {
        auto& sensor = sensors_[index];
        if (reset) {
            ++sensor.idResets;
        }
        for (std::uint64_t i = 0; i < missed; ++i) {
            out.push_back(next(sensor)); // status NoData
            ++sensor.noData;
        }
        auto record = next(sensor);
        record.status = PhotoStatus::Ok;
        record.frameId = meta.frameId;
        record.hostTimestamp = meta.hostTimestamp;
        record.deviceTimestampNs = meta.deviceTimestampNs;
        record.crop =
            toPixels(sensor.config.roi, meta.width, meta.height, cropAlignment(meta.pixelFormat));
        out.push_back(record);
    }
}

void FrameSequenceTracker::onTick(Timestamp /*now*/, std::vector<ObjectRecord>& /*out*/) {
    // P40.40: timing-based miss detection and machine state.
}

std::vector<SensorCounters> FrameSequenceTracker::counters() const {
    std::vector<SensorCounters> out;
    out.reserve(sensors_.size());
    for (const auto& s : sensors_) {
        out.push_back({.laneId = s.config.laneId,
                       .sensorId = s.config.sensorId,
                       .cameraId = s.config.cameraId,
                       .count = s.count,
                       .noData = s.noData,
                       .idResets = s.idResets});
    }
    return out;
}

} // namespace vsort::service

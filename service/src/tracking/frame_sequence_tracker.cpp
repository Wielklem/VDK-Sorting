#include "tracking/frame_sequence_tracker.hpp"

#include <algorithm>
#include <utility>

#include <spdlog/spdlog.h>

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
    std::map<std::uint16_t, std::vector<std::size_t>> laneMembers;
    std::vector<std::uint16_t> laneOrder;
    for (auto& s : sensors) {
        const std::size_t index = sensors_.size();
        auto camera =
            cameras_.try_emplace(s.cameraId, CameraState{CameraTiming{options_.timing}, {}});
        camera.first->second.sensors.push_back(index);
        if (!laneMembers.contains(s.laneId)) {
            laneOrder.push_back(s.laneId);
        }
        laneMembers[s.laneId].push_back(index);
        SensorState state;
        state.config = std::move(s);
        state.stats.laneId = state.config.laneId;
        state.stats.sensorId = state.config.sensorId;
        state.stats.cameraId = state.config.cameraId;
        sensors_.push_back(std::move(state));
    }
    for (const auto laneId : laneOrder) {
        auto& members = laneMembers[laneId];
        for (std::size_t p = 0; p < members.size(); ++p) {
            sensors_[members[p]].lane = lanes_.size();
            sensors_[members[p]].position = p;
        }
        lanes_.push_back(
            LaneState{.laneId = laneId,
                      .sensors = members,
                      .consistency = LaneConsistency{members.size(), options_.consistency}});
    }
}

ObjectRecord FrameSequenceTracker::record(const SensorState& sensor, std::int64_t index) const {
    ObjectRecord r;
    r.laneId = sensor.config.laneId;
    r.sensorId = sensor.config.sensorId;
    r.cameraId = sensor.config.cameraId;
    r.sensorCount = static_cast<std::uint64_t>(std::max<std::int64_t>(index, 0));
    r.cupId = index - static_cast<std::int64_t>(sensor.config.offsetCups);
    return r; // status NoData
}

void FrameSequenceTracker::emitNoData(SensorState& sensor, std::int64_t from, std::int64_t to,
                                      std::vector<ObjectRecord>& out) {
    for (std::int64_t i = std::max<std::int64_t>(from, 0); i < to; ++i) {
        out.push_back(record(sensor, i));
        ++sensor.stats.noData;
    }
}

std::vector<SensorView> FrameSequenceTracker::views(const LaneState& lane) const {
    std::vector<SensorView> out;
    out.reserve(lane.sensors.size());
    for (const auto index : lane.sensors) {
        const auto& s = sensors_[index];
        const auto& timing = cameras_.at(s.config.cameraId).timing;
        out.push_back({.steady = timing.steady(),
                       .hasFrame = s.hasFrame,
                       .lastIndex = s.lastIndex,
                       .lastHost = s.lastHost,
                       .intervalNs = timing.intervalNs().value_or(0.0)});
    }
    return out;
}

void FrameSequenceTracker::apply(SensorState& sensor, const FrameDecision& decision,
                                 std::vector<ObjectRecord>& out) {
    if (decision.extra) {
        ++sensor.stats.extraFrames;
        return;
    }
    if (decision.idReset) {
        ++sensor.stats.idResets;
    }
    sensor.stats.timingMisses += decision.timingMisses;
    const auto& meta = decision.meta;
    const std::int64_t first = sensor.count;
    const std::int64_t provisional = first + static_cast<std::int64_t>(decision.missedBefore);

    auto& lane = lanes_[sensor.lane];
    const auto check =
        lane.consistency.check(sensor.position, provisional, meta.hostTimestamp, views(lane));
    const std::int64_t index = std::max<std::int64_t>(provisional + check.correction, 0);
    if (check.correction == 0) {
        emitNoData(sensor, first, index, out);
    } else {
        // Counting error: the cups since the last agreement are uncertain for this sensor.
        ++sensor.stats.repairs;
        spdlog::warn("tracking: lane {} sensor {} counter {} by {} cup(s) (cross-sensor check)",
                     sensor.config.laneId, sensor.config.sensorId,
                     check.aligned ? "aligned" : "corrected", check.correction);
        const std::int64_t from =
            std::max(sensor.lastGood + 1, index - static_cast<std::int64_t>(options_.markLimit));
        emitNoData(sensor, from, index, out);
    }
    if (check.consistent || check.correction != 0) {
        sensor.lastGood = index;
    }

    auto ok = record(sensor, index);
    ok.status = PhotoStatus::Ok;
    ok.frameId = meta.frameId;
    ok.hostTimestamp = meta.hostTimestamp;
    ok.deviceTimestampNs = meta.deviceTimestampNs;
    ok.crop = toPixels(sensor.config.roi, meta.width, meta.height, cropAlignment(meta.pixelFormat));
    out.push_back(ok);

    sensor.count = index + 1;
    sensor.lastIndex = index;
    sensor.lastHost = meta.hostTimestamp;
    sensor.hasFrame = true;
}

void FrameSequenceTracker::apply(const std::vector<FrameDecision>& decisions,
                                 std::vector<ObjectRecord>& out) {
    for (const auto& decision : decisions) {
        const auto it = cameras_.find(decision.meta.cameraIndex);
        for (const std::size_t index : it->second.sensors) {
            apply(sensors_[index], decision, out);
        }
    }
}

void FrameSequenceTracker::onFrame(const camera::FrameMetadata& meta,
                                   std::vector<ObjectRecord>& out) {
    const auto it = cameras_.find(meta.cameraIndex);
    if (it == cameras_.end()) {
        ++unmappedFrames_;
        return;
    }
    std::vector<FrameDecision> decisions;
    it->second.timing.onFrame(meta, decisions);
    apply(decisions, out);
}

void FrameSequenceTracker::onTick(Timestamp now, std::vector<ObjectRecord>& out) {
    std::vector<FrameDecision> decisions;
    for (auto& [id, camera] : cameras_) {
        camera.timing.onTick(now, decisions);
    }
    apply(decisions, out);
}

nlohmann::json FrameSequenceTracker::state() const {
    nlohmann::json lanes = nlohmann::json::array();
    for (const auto& lane : lanes_) {
        nlohmann::json members = nlohmann::json::array();
        for (std::size_t p = 0; p < lane.sensors.size(); ++p) {
            if (const auto phase = lane.consistency.phaseToKeep(p)) {
                members.push_back(
                    {{"sensor_id", sensors_[lane.sensors[p]].config.sensorId}, {"phase", *phase}});
            }
        }
        lanes.push_back({{"lane_id", lane.laneId}, {"sensors", std::move(members)}});
    }
    return {{"lanes", std::move(lanes)}};
}

void FrameSequenceTracker::restoreState(const nlohmann::json& state) {
    if (!state.is_object() || !state.contains("lanes") || !state.at("lanes").is_array()) {
        return;
    }
    for (const auto& jl : state.at("lanes")) {
        const auto laneId = jl.value("lane_id", -1);
        const auto lane = std::ranges::find(lanes_, laneId, &LaneState::laneId);
        if (lane == lanes_.end() || !jl.contains("sensors") || !jl.at("sensors").is_array()) {
            continue;
        }
        for (const auto& js : jl.at("sensors")) {
            const auto sensorId = js.value("sensor_id", -1);
            for (std::size_t p = 0; p < lane->sensors.size(); ++p) {
                if (sensors_[lane->sensors[p]].config.sensorId == sensorId &&
                    js.contains("phase") && js.at("phase").is_number()) {
                    lane->consistency.restore(p, js.at("phase").get<double>());
                }
            }
        }
    }
}

std::vector<SensorCounters> FrameSequenceTracker::counters() const {
    std::vector<SensorCounters> out;
    out.reserve(sensors_.size());
    for (const auto& s : sensors_) {
        auto c = s.stats;
        c.count = static_cast<std::uint64_t>(s.count);
        c.phase = cameras_.at(s.config.cameraId).timing.phase();
        out.push_back(c);
    }
    return out;
}

} // namespace vsort::service

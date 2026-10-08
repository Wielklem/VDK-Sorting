#include "analysis/result_join.hpp"

#include <algorithm>
#include <utility>

namespace vsort::service {

void ResultJoin::emit(const ObjectRecord& record, const SensorResult& result,
                      std::vector<Measurement>& out) {
    out.push_back(Measurement{.laneId = record.laneId,
                              .cupId = record.cupId,
                              .sensorId = record.sensorId,
                              .cameraId = record.cameraId,
                              .frameId = record.frameId,
                              .values = result.values});
    ++joined_;
}

void ResultJoin::addResult(SensorResult result, std::vector<Measurement>& out) {
    auto& list = results_[result.sensorId];
    // A frame ID seen again (camera reconnected, IDs restarted): the new frame replaces the old.
    std::erase_if(list, [&](const SensorResult& r) { return r.frameId == result.frameId; });
    list.push_back(std::move(result));
    while (list.size() > options_.resultsPerSensor) {
        list.pop_front();
    }
    const auto& stored = list.back();
    std::erase_if(waiting_, [&](const Waiting& w) {
        if (w.record.sensorId != stored.sensorId || w.record.frameId != stored.frameId) {
            return false;
        }
        emit(w.record, stored, out);
        return true;
    });
}

void ResultJoin::addRecord(const ObjectRecord& record, Timestamp now,
                           std::vector<Measurement>& out) {
    if (record.status != PhotoStatus::Ok) {
        return;
    }
    if (const auto it = results_.find(record.sensorId); it != results_.end()) {
        const auto& list = it->second;
        const auto match =
            std::ranges::find(list.rbegin(), list.rend(), record.frameId, &SensorResult::frameId);
        if (match != list.rend()) {
            emit(record, *match, out);
            return;
        }
    }
    // A correction of a record that is still waiting replaces it.
    std::erase_if(waiting_, [&](const Waiting& w) {
        return w.record.sensorId == record.sensorId && w.record.frameId == record.frameId;
    });
    waiting_.push_back({.record = record, .since = now});
    while (waiting_.size() > options_.maxWaiting) {
        waiting_.pop_front();
        ++expired_;
    }
}

void ResultJoin::expire(Timestamp now) {
    while (!waiting_.empty() && now - waiting_.front().since > options_.recordWait) {
        waiting_.pop_front();
        ++expired_;
    }
}

} // namespace vsort::service

#include "cups/cup_table.hpp"

#include <algorithm>
#include <iterator>

namespace vsort::service {

CupTable::CupTable(const MachineConfig& machine, CupTableOptions options)
    : options_{options} {
    options_.depth = std::max<std::size_t>(options_.depth, 1);
    for (const auto* lane : machine.lanes()) {
        Lane l;
        l.laneId = lane->id;
        std::uint32_t smallest = 0;
        if (!lane->sensors.empty()) {
            smallest = std::ranges::min(lane->sensors, {}, &SensorConfig::offsetCups).offsetCups;
        }
        for (const auto& s : lane->sensors) {
            l.sensorIds.push_back(s.id);
            l.dueAfter.push_back(s.offsetCups - smallest);
        }
        lanes_.push_back(std::move(l));
    }
}

CupState& CupTable::cup(Lane& lane, std::int64_t cupId) {
    auto [it, inserted] = lane.cups.try_emplace(cupId);
    if (inserted) {
        it->second.cupId = cupId;
        for (const auto id : lane.sensorIds) {
            it->second.cells.push_back({.sensorId = id, .status = CellStatus::Pending});
        }
        lane.dirty.insert(cupId);
    }
    return it->second;
}

bool CupTable::apply(const ObjectRecord& record) {
    const auto lane = std::ranges::find(lanes_, record.laneId, &Lane::laneId);
    if (lane == lanes_.end()) {
        ++ignored_;
        return false;
    }
    const auto sensor = std::ranges::find(lane->sensorIds, record.sensorId);
    if (sensor == lane->sensorIds.end()) {
        ++ignored_;
        return false;
    }
    const auto depth = static_cast<std::int64_t>(options_.depth);
    const std::int64_t newest = lane->cups.empty() ? record.cupId : lane->cups.rbegin()->first;
    if (record.cupId <= newest - depth) {
        ++ignored_;
        return false;
    }
    // Fill gaps (at most one window) so the cup IDs stay continuous.
    if (!lane->cups.empty() && record.cupId > newest + 1) {
        for (std::int64_t id = std::max(newest + 1, record.cupId - depth + 1); id < record.cupId;
             ++id) {
            (void)cup(*lane, id);
        }
    } else if (!lane->cups.empty() && record.cupId < lane->cups.begin()->first) {
        const std::int64_t oldest = lane->cups.begin()->first;
        for (std::int64_t id = record.cupId + 1; id < oldest; ++id) {
            (void)cup(*lane, id);
        }
    }
    auto& state = cup(*lane, record.cupId);
    auto& cell =
        state.cells[static_cast<std::size_t>(std::distance(lane->sensorIds.begin(), sensor))];
    const auto status = record.status == PhotoStatus::Ok ? CellStatus::Ok : CellStatus::NoData;
    if (cell.status != status) {
        cell.status = status;
        lane->dirty.insert(record.cupId);
    }
    while (lane->cups.size() > options_.depth) {
        lane->dirty.erase(lane->cups.begin()->first);
        lane->cups.erase(lane->cups.begin());
    }
    markPassed(*lane);
    return true;
}

void CupTable::markPassed(Lane& lane) {
    if (lane.cups.empty()) {
        return;
    }
    const std::int64_t newest = lane.cups.rbegin()->first;
    for (auto& [id, state] : lane.cups) {
        for (std::size_t i = 0; i < state.cells.size(); ++i) {
            const auto due = static_cast<std::int64_t>(lane.dueAfter[i] + options_.passMargin);
            if (state.cells[i].status == CellStatus::Pending && newest - id > due) {
                state.cells[i].status = CellStatus::NoData;
                lane.dirty.insert(id);
            }
        }
    }
}

std::vector<CupUpdate> CupTable::takeUpdates() {
    std::vector<CupUpdate> out;
    for (auto& lane : lanes_) {
        if (lane.dirty.empty()) {
            continue;
        }
        CupUpdate update{.laneId = lane.laneId, .seq = ++lane.seq, .cups = {}};
        for (auto it = lane.dirty.rbegin(); it != lane.dirty.rend(); ++it) {
            if (const auto found = lane.cups.find(*it); found != lane.cups.end()) {
                update.cups.push_back(found->second);
            }
        }
        lane.dirty.clear();
        out.push_back(std::move(update));
    }
    return out;
}

std::vector<LaneSnapshot> CupTable::snapshot(std::optional<std::uint16_t> laneId) const {
    std::vector<LaneSnapshot> out;
    for (const auto& lane : lanes_) {
        if (laneId && *laneId != lane.laneId) {
            continue;
        }
        LaneSnapshot s{.laneId = lane.laneId,
                       .seq = lane.seq,
                       .depth = static_cast<std::uint16_t>(options_.depth),
                       .cups = {}};
        for (auto it = lane.cups.rbegin(); it != lane.cups.rend(); ++it) {
            s.cups.push_back(it->second);
        }
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace vsort::service

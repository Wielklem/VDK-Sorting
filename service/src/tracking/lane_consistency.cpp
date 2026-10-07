#include "tracking/lane_consistency.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace vsort::service {

LaneConsistency::LaneConsistency(std::size_t sensorCount, ConsistencyOptions options)
    : options_{options}
    , states_(sensorCount) {}

// Groups the values by whole-cup distance to the most upstream one; the largest group wins,
// a tie goes to the group of the most upstream member. Returns the median of that group.
double LaneConsistency::reference(const std::vector<Value>& values) {
    const double upstream = values.front().value; // values are in lane order
    std::map<std::int64_t, std::vector<double>> groups;
    std::vector<std::int64_t> keys;
    for (const auto& v : values) {
        const auto key = std::llround(v.value - upstream);
        groups[key].push_back(v.value);
        keys.push_back(key);
    }
    std::size_t best = 0;
    for (const auto& [key, members] : groups) {
        best = std::max(best, members.size());
    }
    for (const auto key : keys) { // lane order: the first full-size group wins the tie
        auto& members = groups[key];
        if (members.size() == best) {
            std::ranges::sort(members);
            return members[(members.size() - 1) / 2];
        }
    }
    return upstream;
}

ConsistencyResult LaneConsistency::check(std::size_t sensor, std::int64_t index, Timestamp t,
                                         std::span<const SensorView> views) {
    auto& me = states_.at(sensor);
    if (!views[sensor].steady) {
        me.streak = 0;
        return {};
    }
    std::vector<Value> others;
    for (std::size_t r = 0; r < views.size(); ++r) {
        const auto& v = views[r];
        if (r == sensor || !v.steady || !v.hasFrame || !states_[r].baseline ||
            v.intervalNs <= 0.0) {
            continue;
        }
        const double age = static_cast<double>((t - v.lastHost).count());
        if (age > 2.5 * v.intervalNs) {
            continue; // too old to extrapolate
        }
        others.push_back({.sensor = r,
                          .value = static_cast<double>(v.lastIndex) + (age / v.intervalNs) -
                                   *states_[r].baseline});
    }
    if (others.empty()) {
        const bool anyBaseline =
            std::ranges::any_of(states_, [](const State& st) { return st.baseline.has_value(); });
        if (!anyBaseline) {
            me.baseline = me.stored.value_or(0.0); // the first steady sensor anchors the lane
            return {.correction = 0, .consistent = true, .aligned = false};
        }
        return {}; // nothing to compare with right now
    }

    if (!me.baseline) {
        // First comparison: the whole-cup part is a counting difference (the camera started in
        // another machine step), the rest is the phase. Convention without history: the phase
        // is within half a cup of the lane.
        const double d = static_cast<double>(index) - reference(others);
        double phase = d - static_cast<double>(std::llround(d));
        if (me.stored) {
            const double fromStored = d - *me.stored;
            if (std::abs(fromStored - static_cast<double>(std::llround(fromStored))) <
                options_.jumpTolerance) {
                phase = *me.stored + (fromStored - static_cast<double>(std::llround(fromStored)));
            }
        }
        const auto whole = std::llround(d - phase);
        me.baseline = phase;
        return {.correction = -whole, .consistent = true, .aligned = whole != 0};
    }

    const double mine = static_cast<double>(index) - *me.baseline;
    std::vector<Value> all = others;
    all.push_back({.sensor = sensor, .value = mine});
    std::ranges::sort(all, {}, &Value::sensor);
    const double deviation = mine - reference(all);

    if (std::abs(deviation) < options_.tolerance) {
        me.streak = 0;
        me.unclear = 0;
        *me.baseline += options_.baselineRate * deviation;
        return {.correction = 0, .consistent = true, .aligned = false};
    }
    const auto jump = std::llround(deviation);
    const double rest = deviation - static_cast<double>(jump);
    if (std::abs(rest) >= options_.jumpTolerance) {
        // Between whole cups: slow drift of the phase, or a wrong phase. Follow small drift;
        // re-learn the phase (as a repair) when it stays unclear.
        me.streak = 0;
        if (std::abs(deviation) < 0.5) {
            *me.baseline += options_.baselineRate * deviation;
        }
        if (++me.unclear >= options_.relearnAfter) {
            me.unclear = 0;
            *me.baseline += rest;
            return {.correction = -jump, .consistent = false, .aligned = false};
        }
        return {};
    }
    me.unclear = 0;
    if (jump != 0) {
        if (jump == me.streakJump) {
            ++me.streak;
        } else {
            me.streak = 1;
            me.streakJump = jump;
        }
        if (me.streak >= options_.confirm) {
            me.streak = 0;
            return {.correction = -jump, .consistent = false, .aligned = false};
        }
        return {};
    }
    me.streak = 0; // close to the lane, outside the tolerance: no action
    return {};
}

} // namespace vsort::service

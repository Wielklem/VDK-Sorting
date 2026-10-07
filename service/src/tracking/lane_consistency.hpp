#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <vsort/common/timestamp.hpp>

namespace vsort::service {

struct ConsistencyOptions {
    double tolerance{0.25};         // |deviation| below this: consistent (in cups)
    double jumpTolerance{0.3};      // deviation this close to a whole number: a counting error
    std::uint32_t confirm{2};       // ... seen this many times in a row before repairing
    double baselineRate{0.1};       // how fast the learned phase follows slow drift
    std::uint32_t relearnAfter{10}; // deviation between whole cups this often: re-learn phase
};

// What the check knows about one sensor of the lane.
struct SensorView {
    bool steady{false}; // its camera runs at constant speed (CameraTiming::steady)
    bool hasFrame{false};
    std::int64_t lastIndex{0}; // cup index of its last frame
    Timestamp lastHost;        // host time of that frame
    double intervalNs{0.0};    // its predicted single-cup interval
};

struct ConsistencyResult {
    std::int64_t correction{0}; // add to the sensor's cup index (0 = none)
    bool consistent{false};     // the index agrees with the lane
    bool aligned{false};        // correction is the first alignment (sensor started late)
};

// P40.40 (d): cross-sensor check for one lane. Every sensor's cup count, extrapolated to the
// same moment and minus its learned phase (fraction of a cup, includes the camera's latency),
// must agree. Majority vote; a tie goes to the most upstream sensor. A sensor that disagrees by
// a whole number of cups, twice in a row, is corrected. Only steady cameras take part, so the
// check pauses during ramps and catches up (and repairs) once the speed is constant again.
class LaneConsistency {
public:
    LaneConsistency(std::size_t sensorCount, ConsistencyOptions options = {});

    // `sensor` (lane order, upstream first) got cup index `index` at host time `t`.
    // `views` holds every sensor of the lane in lane order.
    [[nodiscard]] ConsistencyResult check(std::size_t sensor, std::int64_t index, Timestamp t,
                                          std::span<const SensorView> views);

    [[nodiscard]] std::optional<double> baseline(std::size_t sensor) const {
        return states_.at(sensor).baseline;
    }
    // Phase learned in an earlier run (persisted): keeps the cup convention the same across
    // service restarts. Ignored when it no longer fits.
    void restore(std::size_t sensor, double phase) { states_.at(sensor).stored = phase; }
    // What to persist: the learned phase, else the one restored from an earlier run.
    [[nodiscard]] std::optional<double> phaseToKeep(std::size_t sensor) const {
        const auto& st = states_.at(sensor);
        return st.baseline ? st.baseline : st.stored;
    }

private:
    struct State {
        std::optional<double> baseline; // learned phase relative to the lane, in cups
        std::optional<double> stored;   // phase from an earlier run
        std::int64_t streakJump{0};
        std::uint32_t streak{0};
        std::uint32_t unclear{0};
    };
    struct Value {
        std::size_t sensor{0};
        double value{0.0};
    };

    [[nodiscard]] static double reference(const std::vector<Value>& values);

    ConsistencyOptions options_;
    std::vector<State> states_;
};

} // namespace vsort::service

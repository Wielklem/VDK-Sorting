// P40.40: miss detection and speed handling with synthetic trigger sequences (no hardware).
// A simulated chain moves cups past 4 sensors; each sensor has its own trigger at a fixed
// position (phase, fraction of a cup) and its own latency and clock.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "tracking/frame_sequence_tracker.hpp"

using namespace vsort;
using namespace vsort::service;

namespace {

using Speed = std::function<double(double)>; // cups per second at time t (s)

struct SimSensor {
    double phase{0.0};        // trigger position within the cup pitch
    double latencyMs{5.0};    // trigger -> host arrival (USB ~5, GigE ~25)
    double clockOffsetS{0.0}; // camera clock vs. simulation time
};

// Default V1 layout: 2 USB + 2 GigE cameras.
std::vector<SimSensor> fourSensors() {
    return {{.phase = 0.10, .latencyMs = 5.0, .clockOffsetS = 11.0},
            {.phase = 0.30, .latencyMs = 6.0, .clockOffsetS = 250.0},
            {.phase = 0.25, .latencyMs = 25.0, .clockOffsetS = 3.0},
            {.phase = 0.15, .latencyMs = 28.0, .clockOffsetS = 77.0}};
}

struct Shot {
    std::size_t sensor{0};
    std::int64_t cup{0};
    double time{0.0}; // trigger time (s)
};

// Integrates the speed; a sensor triggers when the chain position passes cup + phase.
std::vector<Shot> simulate(const Speed& speed, double duration,
                           const std::vector<SimSensor>& sensors) {
    constexpr double kDt = 1e-4;
    std::vector<Shot> shots;
    std::vector<std::int64_t> next(sensors.size(), 0);
    double x = 0.0;
    for (double t = 0.0; t < duration; t += kDt) {
        const double v = std::max(speed(t), 0.0);
        const double x2 = x + (v * kDt);
        for (std::size_t s = 0; s < sensors.size(); ++s) {
            while (static_cast<double>(next[s]) + sensors[s].phase <= x2) {
                const double target = static_cast<double>(next[s]) + sensors[s].phase;
                const double frac = v > 0.0 ? (target - x) / (v * kDt) : 0.0;
                shots.push_back({.sensor = s, .cup = next[s], .time = t + (frac * kDt)});
                ++next[s];
            }
        }
        x = x2;
    }
    return shots;
}

struct Scenario {
    std::vector<SimSensor> sensors = fourSensors();
    std::set<std::pair<std::size_t, std::int64_t>> missed;     // no trigger: no frame
    std::set<std::pair<std::size_t, std::int64_t>> extraAfter; // double trigger 8 ms later
    std::map<std::size_t, std::int64_t> startsAtCup;           // camera starts late
    std::set<std::pair<std::size_t, std::int64_t>> outage;     // camera offline (no frames)
    std::map<std::size_t, std::int64_t> idResetAtCup;          // frame ID restarts at 0
    double hostJitterMs{0.5};
    double triggerJitterMs{0.05};
    nlohmann::json restore; // tracker state from an earlier run
};

struct SimResult {
    std::map<std::pair<std::size_t, std::int64_t>, PhotoStatus> status; // final, per cup index
    std::vector<SensorCounters> counters;
    std::vector<std::int64_t> cupsSeen; // per sensor: cups that passed it
    nlohmann::json state;
};

// Small deterministic generator (std distributions differ between standard libraries).
class Lcg {
public:
    double uniform() { // [-1, 1)
        state_ = (state_ * 6364136223846793005ULL) + 1442695040888963407ULL;
        return (static_cast<double>(state_ >> 11) / 9007199254740992.0 * 2.0) - 1.0;
    }

private:
    std::uint64_t state_{42};
};

SimResult run(const Speed& speed, double duration, const Scenario& sc) {
    const auto shots = simulate(speed, duration, sc.sensors);
    std::vector<TrackedSensor> tracked;
    for (std::size_t s = 0; s < sc.sensors.size(); ++s) {
        tracked.push_back({.laneId = 1,
                           .sensorId = static_cast<std::uint16_t>(s + 1),
                           .cameraId = static_cast<std::uint16_t>(s),
                           .offsetCups = 0,
                           .roi = {}});
    }
    FrameSequenceTracker tracker{tracked};
    tracker.restoreState(sc.restore);

    Lcg rng;
    std::vector<camera::FrameMetadata> frames;
    std::vector<std::uint64_t> frameIds(sc.sensors.size(), 1000);
    SimResult result;
    result.cupsSeen.assign(sc.sensors.size(), 0);
    for (const auto& shot : shots) {
        const auto key = std::make_pair(shot.sensor, shot.cup);
        result.cupsSeen[shot.sensor] = shot.cup + 1;
        const auto late = sc.startsAtCup.find(shot.sensor);
        if (late != sc.startsAtCup.end() && shot.cup < late->second) {
            continue;
        }
        if (sc.missed.contains(key) || sc.outage.contains(key)) {
            continue;
        }
        if (const auto reset = sc.idResetAtCup.find(shot.sensor);
            reset != sc.idResetAtCup.end() && reset->second == shot.cup) {
            frameIds[shot.sensor] = 0;
        }
        const auto& sensor = sc.sensors[shot.sensor];
        auto add = [&](double triggerTime) {
            const double trigger = triggerTime + (sc.triggerJitterMs * 1e-3 * rng.uniform());
            const double host =
                trigger + ((sensor.latencyMs + (sc.hostJitterMs * rng.uniform())) * 1e-3);
            camera::FrameMetadata m;
            m.cameraIndex = static_cast<std::uint16_t>(shot.sensor);
            m.frameId = FrameId{frameIds[shot.sensor]++};
            m.hostTimestamp = Timestamp{std::chrono::nanoseconds{std::llround(host * 1e9)}};
            m.deviceTimestampNs =
                static_cast<std::uint64_t>(std::llround((trigger + sensor.clockOffsetS) * 1e9));
            m.width = 640;
            m.height = 480;
            frames.push_back(m);
        };
        add(shot.time);
        if (sc.extraAfter.contains(key)) {
            add(shot.time + 0.008);
        }
    }
    std::ranges::sort(frames, {}, &camera::FrameMetadata::hostTimestamp);

    std::vector<ObjectRecord> records;
    auto collect = [&] {
        for (const auto& r : records) {
            result.status[{r.cameraId, static_cast<std::int64_t>(r.sensorCount)}] = r.status;
        }
        records.clear();
    };
    const auto tick = std::chrono::milliseconds{100};
    Timestamp nextTick{tick};
    for (const auto& f : frames) {
        while (nextTick <= f.hostTimestamp) {
            tracker.onTick(nextTick, records);
            nextTick = nextTick + tick;
        }
        tracker.onFrame(f, records);
        collect();
    }
    const Timestamp end{std::chrono::nanoseconds{std::llround((duration + 3.0) * 1e9)}};
    while (nextTick <= end) {
        tracker.onTick(nextTick, records);
        nextTick = nextTick + tick;
    }
    collect();
    result.counters = tracker.counters();
    result.state = tracker.state();
    return result;
}

// Sensor count minus physical cups: the lane's cup numbering for this sensor. Phases more than
// half a cup apart give +-1 (convention); it must not change during a run.
std::int64_t shift(const SimResult& r, std::size_t s) {
    return static_cast<std::int64_t>(r.counters[s].count) - r.cupsSeen[s];
}

// Physical cups in [from, to) of sensor s whose final status is not the expected one.
std::vector<std::int64_t> wrong(const SimResult& r, std::size_t s, std::int64_t from,
                                std::int64_t to, const std::set<std::int64_t>& noData) {
    std::vector<std::int64_t> out;
    const auto k = shift(r, s);
    for (std::int64_t cup = from; cup < to; ++cup) {
        const auto it = r.status.find({s, cup + k});
        const auto expected = noData.contains(cup) ? PhotoStatus::NoData : PhotoStatus::Ok;
        if (it == r.status.end() || it->second != expected) {
            out.push_back(cup);
        }
    }
    return out;
}

void expectClean(const SimResult& r) {
    for (std::size_t s = 0; s < r.counters.size(); ++s) {
        SCOPED_TRACE("sensor " + std::to_string(s));
        EXPECT_EQ(static_cast<std::int64_t>(r.counters[s].count), r.cupsSeen[s]);
        EXPECT_EQ(r.counters[s].noData, 0U);
        EXPECT_EQ(r.counters[s].repairs, 0U);
        EXPECT_EQ(r.counters[s].extraFrames, 0U);
        EXPECT_TRUE(wrong(r, s, 0, r.cupsSeen[s], {}).empty());
    }
}

double constant(double /*t*/) {
    return 10.0;
}

// Ramp up over `up` s, run, ramp down over `down` s ending at `stopAt`, then stand still.
Speed rampProfile(double up, double stopAt, double down) {
    return [=](double t) {
        if (t < up) {
            return 10.0 * t / up;
        }
        if (t < stopAt - down) {
            return 10.0;
        }
        if (t < stopAt) {
            return 10.0 * (stopAt - t) / down;
        }
        return 0.0;
    };
}

} // namespace

TEST(SpeedHandling, ConstantSpeed) {
    const auto r = run(constant, 20.0, {});
    expectClean(r);
    EXPECT_EQ(r.cupsSeen[0], 200);
    EXPECT_EQ(r.counters[0].phase, CameraPhase::Silent); // stopped after the run
}

TEST(SpeedHandling, LinearRampUpAndDown) {
    for (const double ramp : {1.0, 3.0, 5.0}) {
        SCOPED_TRACE("ramp " + std::to_string(ramp));
        expectClean(run(rampProfile(ramp, 20.0, ramp), 23.0, {}));
    }
}

TEST(SpeedHandling, EmergencyStopAndRestart) {
    const Speed speed = [](double t) {
        if (t < 8.0) {
            return 10.0;
        }
        if (t < 10.0) {
            return 0.0; // abrupt stop
        }
        return std::min(10.0, 10.0 * (t - 10.0) / 2.0); // 2 s ramp up
    };
    expectClean(run(speed, 20.0, {}));
}

TEST(SpeedHandling, ShortHaltShorterThanStopTimeout) {
    for (const double halt : {0.15, 0.3, 0.6}) {
        SCOPED_TRACE("halt " + std::to_string(halt));
        const Speed speed = [halt](double t) {
            if (t < 6.05) {
                return 10.0;
            }
            if (t < 6.05 + halt) {
                return 0.0;
            }
            return std::min(10.0, 10.0 * (t - 6.05 - halt));
        };
        expectClean(run(speed, 14.0, {}));
    }
}

TEST(SpeedHandling, MissesAtSteadySpeed) {
    Scenario sc;
    sc.missed = {{0, 50}, {3, 80}, {1, 120}, {1, 121}}; // first and last sensor, and a double
    const auto r = run(constant, 20.0, sc);
    for (std::size_t s = 0; s < 4; ++s) {
        SCOPED_TRACE("sensor " + std::to_string(s));
        std::set<std::int64_t> expected;
        for (const auto& [sensor, cup] : sc.missed) {
            if (sensor == s) {
                expected.insert(cup);
            }
        }
        EXPECT_EQ(static_cast<std::int64_t>(r.counters[s].count), r.cupsSeen[s]);
        EXPECT_TRUE(wrong(r, s, 0, r.cupsSeen[s], expected).empty());
        EXPECT_EQ(r.counters[s].timingMisses, expected.size());
        EXPECT_EQ(r.counters[s].repairs, 0U);
    }
}

TEST(SpeedHandling, EmptyCupsMissedByAllSensors) {
    Scenario sc; // the same cup position triggers no sensor (all miss the same step)
    for (std::size_t s = 0; s < 4; ++s) {
        sc.missed.insert({s, 70});
    }
    const auto r = run(constant, 15.0, sc);
    for (std::size_t s = 0; s < 4; ++s) {
        EXPECT_TRUE(wrong(r, s, 0, r.cupsSeen[s], {70}).empty());
        EXPECT_EQ(static_cast<std::int64_t>(r.counters[s].count), r.cupsSeen[s]);
    }
}

TEST(SpeedHandling, MissDuringRampIsRepairedLater) {
    Scenario sc;
    sc.missed = {{1, 4}, {2, 196}}; // during ramp up and during ramp down
    const Speed speed = [](double t) {
        if (t < 3.0) {
            return 10.0 * t / 3.0; // ramp up (cups 0..14)
        }
        if (t < 20.0) {
            return 10.0;
        }
        if (t < 23.0) {
            return 10.0 * (23.0 - t) / 3.0; // ramp down
        }
        if (t < 25.0) {
            return 0.0;
        }
        return std::min(10.0, 10.0 * (t - 25.0) / 3.0); // restart
    };
    const auto r = run(speed, 40.0, sc);
    for (std::size_t s = 0; s < 4; ++s) {
        SCOPED_TRACE("sensor " + std::to_string(s));
        EXPECT_EQ(static_cast<std::int64_t>(r.counters[s].count), r.cupsSeen[s]);
        // The last 50 cups are all fine, whatever happened before.
        EXPECT_TRUE(wrong(r, s, r.cupsSeen[s] - 50, r.cupsSeen[s], {}).empty());
    }
    EXPECT_EQ(r.status.at({1, 4}), PhotoStatus::NoData);
    EXPECT_EQ(r.status.at({2, 196}), PhotoStatus::NoData);
    EXPECT_TRUE(wrong(r, 0, 0, r.cupsSeen[0], {}).empty()); // unaffected sensors stay clean
    EXPECT_TRUE(wrong(r, 3, 0, r.cupsSeen[3], {}).empty());
}

TEST(SpeedHandling, Jitter) {
    Scenario sc;
    sc.hostJitterMs = 4.0;
    sc.triggerJitterMs = 1.0;
    expectClean(run(rampProfile(2.0, 40.0, 2.0), 42.0, sc));
}

TEST(SpeedHandling, AllCamerasSilentGivesNoCups) {
    const Speed speed = [](double t) { return (t < 5.0 || t > 15.0) ? 10.0 : 0.0; };
    const auto r = run(speed, 25.0, {});
    expectClean(r);
    EXPECT_EQ(r.cupsSeen[0], 150); // 50 + 100, nothing invented during the 10 s standstill
}

TEST(SpeedHandling, DoubleTriggerIsDropped) {
    Scenario sc;
    sc.extraAfter = {{2, 60}};
    const auto r = run(constant, 12.0, sc);
    EXPECT_EQ(r.counters[2].extraFrames, 1U);
    EXPECT_TRUE(wrong(r, 2, 0, r.cupsSeen[2], {}).empty());
    EXPECT_EQ(static_cast<std::int64_t>(r.counters[2].count), r.cupsSeen[2]);
}

TEST(SpeedHandling, CameraOutageIsRepairedByTheOtherSensors) {
    Scenario sc;
    for (std::int64_t cup = 100; cup < 130; ++cup) { // 3 s offline, then it restarts
        sc.outage.insert({3, cup});
    }
    sc.idResetAtCup = {{3, 130}};
    const auto r = run(constant, 25.0, sc);
    EXPECT_EQ(static_cast<std::int64_t>(r.counters[3].count), r.cupsSeen[3]);
    EXPECT_GE(r.counters[3].repairs, 1U);
    for (std::int64_t cup = 100; cup < 130; ++cup) {
        EXPECT_EQ(r.status.at({3, cup}), PhotoStatus::NoData) << cup;
    }
    EXPECT_TRUE(wrong(r, 3, 150, r.cupsSeen[3], {}).empty());
    EXPECT_TRUE(wrong(r, 3, 0, 100, {}).empty());
    for (std::size_t s = 0; s < 3; ++s) {
        EXPECT_TRUE(wrong(r, s, 0, r.cupsSeen[s], {}).empty());
    }
}

TEST(SpeedHandling, CameraStartingLateIsAligned) {
    Scenario sc;
    sc.startsAtCup = {{1, 7}}; // service started while the machine was running
    const auto r = run(constant, 15.0, sc);
    EXPECT_EQ(static_cast<std::int64_t>(r.counters[1].count), r.cupsSeen[1]);
    EXPECT_EQ(r.counters[1].repairs, 1U);
    EXPECT_TRUE(wrong(r, 1, 40, r.cupsSeen[1], {}).empty());
}

TEST(SpeedHandling, PhasesMoreThanHalfACupApart) {
    Scenario sc;
    sc.sensors[2].phase = 0.75; // 0.65 cups after sensor 1: numbered as the next cup
    sc.missed = {{2, 90}};
    const auto r = run(rampProfile(3.0, 25.0, 3.0), 28.0, sc);
    EXPECT_EQ(shift(r, 2), 1);
    EXPECT_TRUE(wrong(r, 2, 40, r.cupsSeen[2], {90}).empty());
    for (const std::size_t s : {0U, 1U, 3U}) {
        EXPECT_EQ(shift(r, s), 0);
        EXPECT_TRUE(wrong(r, s, 0, r.cupsSeen[s], {}).empty());
    }
}

TEST(SpeedHandling, LearnedPhasesKeepTheNumberingAfterARestart) {
    Scenario first;
    first.sensors[2].phase = 0.57; // about half a cup from sensor 1
    const auto r1 = run(constant, 15.0, first);

    Scenario second = first; // service restarted; sensor 1 comes up late this time
    second.startsAtCup = {{0, 25}};
    second.restore = r1.state;
    const auto r2 = run(constant, 15.0, second);
    for (std::size_t s = 1; s < 4; ++s) {
        SCOPED_TRACE("sensor " + std::to_string(s));
        EXPECT_EQ(shift(r2, s) - shift(r2, 0), shift(r1, s) - shift(r1, 0));
        EXPECT_TRUE(wrong(r2, s, 60, r2.cupsSeen[s], {}).empty());
    }
    ASSERT_TRUE(r1.state.contains("lanes"));
    EXPECT_EQ(r1.state["lanes"][0]["sensors"].size(), 4U);
}

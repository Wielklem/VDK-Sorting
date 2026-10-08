#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

#include <vsort/common/timestamp.hpp>
#include <vsort/common/types.hpp>

#include "analysis/measurement.hpp"
#include "tracking/object_record.hpp"

namespace vsort::service {

// What the pipeline measured for one sensor in one frame.
struct SensorResult {
    std::uint16_t sensorId{0};
    FrameId frameId;
    std::vector<MeasurementValue> values;
};

struct JoinOptions {
    std::size_t resultsPerSensor{64};           // newest results kept per sensor
    std::chrono::milliseconds recordWait{2000}; // a record waits this long for its result
    std::size_t maxWaiting{1024};               // waiting records; the oldest are dropped
};

// P60.10: pairs the analysis results (per sensor and frame, from the camera threads) with the
// ObjectRecords from tracking (which cup that frame is) into Measurements (MSG-60-01). Either can
// come first. Results are kept after a match, so a corrected record for the same frame (P40.40)
// joins again. Not thread-safe: one thread calls everything.
class ResultJoin {
public:
    explicit ResultJoin(JoinOptions options = {})
        : options_{options} {}

    // Appends a Measurement for every record that was waiting for this result.
    void addResult(SensorResult result, std::vector<Measurement>& out);

    // Ok records are joined now or when their result arrives; NoData records are ignored.
    void addRecord(const ObjectRecord& record, Timestamp now, std::vector<Measurement>& out);

    // Drops records that waited longer than recordWait (their frame was not analysed).
    void expire(Timestamp now);

    [[nodiscard]] std::uint64_t joined() const noexcept { return joined_; }
    [[nodiscard]] std::uint64_t expired() const noexcept { return expired_; }
    [[nodiscard]] std::size_t waiting() const noexcept { return waiting_.size(); }

private:
    struct Waiting {
        ObjectRecord record;
        Timestamp since;
    };

    void emit(const ObjectRecord& record, const SensorResult& result,
              std::vector<Measurement>& out);

    JoinOptions options_;
    std::map<std::uint16_t, std::deque<SensorResult>> results_; // per sensor, newest last
    std::deque<Waiting> waiting_;                               // oldest first
    std::uint64_t joined_{0};
    std::uint64_t expired_{0};
};

} // namespace vsort::service

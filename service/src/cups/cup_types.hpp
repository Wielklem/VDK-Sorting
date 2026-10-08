#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <vsort/common/types.hpp>

#include "analysis/measurement.hpp"

namespace vsort::service {

// P80.100: what the Product Monitor shows per cup and sensor.
enum class CellStatus : std::uint8_t {
    Pending = 0, // the cup has not reached this sensor yet
    Ok = 1,      // photo taken
    NoData = 2,  // missed, lost, or passed the sensor without a photo
};

struct CupCell {
    std::uint16_t sensorId{0};
    CellStatus status{CellStatus::Pending};
    FrameId frameId{}; // the photo (Ok only); a Measurement must be of this frame
    std::vector<MeasurementValue> measurements{}; // P60.15: from the analysis of that photo

    bool operator==(const CupCell&) const = default;
};

struct CupState {
    std::int64_t cupId{0};
    std::vector<CupCell> cells; // every sensor of the lane, lane order (upstream first)

    bool operator==(const CupState&) const = default;
};

// MSG-50-02 on the message bus: the cups of one lane that changed since the last update, with
// their full state, newest first. `seq` increases by one per update of that lane.
struct CupUpdate {
    std::uint16_t laneId{0};
    std::uint64_t seq{0};
    std::vector<CupState> cups;
};

// The last cups of one lane (newest first) and the seq of the last update they include.
struct LaneSnapshot {
    std::uint16_t laneId{0};
    std::uint64_t seq{0};
    std::uint16_t depth{0}; // number of cups kept per lane
    std::vector<CupState> cups;
};

// Read access for the IPC snapshot request. Thread-safe.
class ICupSource {
public:
    ICupSource() = default;
    virtual ~ICupSource() = default;
    ICupSource(const ICupSource&) = delete;
    ICupSource& operator=(const ICupSource&) = delete;
    ICupSource(ICupSource&&) = delete;
    ICupSource& operator=(ICupSource&&) = delete;

    // nullopt = all lanes. An unknown lane gives an empty vector.
    [[nodiscard]] virtual std::vector<LaneSnapshot>
    snapshot(std::optional<std::uint16_t> laneId) const = 0;
};

} // namespace vsort::service

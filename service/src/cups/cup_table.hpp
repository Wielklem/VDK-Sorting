#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include <vsort/common/machine_config.hpp>

#include "cups/cup_types.hpp"
#include "tracking/object_record.hpp"

namespace vsort::service {

struct CupTableOptions {
    std::size_t depth{50};       // cups kept per lane
    std::uint32_t passMargin{3}; // a pending cell becomes NoData this many cups after it is due
};

// P80.100: the last `depth` cups per lane, built from ObjectRecords (MSG-50-01). Not thread-safe.
// - A record creates its cup if needed; cups between the old and the new newest cup are created
//   too (pending), so the cup IDs stay continuous. Older than the window: ignored.
// - A later record for the same lane, cup and sensor replaces the earlier one.
// - Cup c is due at sensor s when the newest cup ID passes c + (offset_s - smallest offset).
//   A cell still pending `passMargin` cups after that becomes NoData (camera silent or record
//   lost). Nothing changes while the machine stands still.
class CupTable {
public:
    explicit CupTable(const MachineConfig& machine, CupTableOptions options = {});

    // False: ignored (unknown lane or sensor, or older than the window).
    bool apply(const ObjectRecord& record);

    // One update per lane with changes since the last call (seq + 1 each).
    [[nodiscard]] std::vector<CupUpdate> takeUpdates();

    // nullopt = all lanes.
    [[nodiscard]] std::vector<LaneSnapshot> snapshot(std::optional<std::uint16_t> laneId) const;

    [[nodiscard]] std::uint64_t ignored() const noexcept { return ignored_; }

private:
    struct Lane {
        std::uint16_t laneId{0};
        std::vector<std::uint16_t> sensorIds; // lane order
        std::vector<std::uint32_t> dueAfter;  // offset - smallest offset of the lane
        std::map<std::int64_t, CupState> cups;
        std::set<std::int64_t> dirty;
        std::uint64_t seq{0};
    };

    CupState& cup(Lane& lane, std::int64_t cupId);
    void markPassed(Lane& lane);

    CupTableOptions options_;
    std::vector<Lane> lanes_;
    std::uint64_t ignored_{0};
};

} // namespace vsort::service

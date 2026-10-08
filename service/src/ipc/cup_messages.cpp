#include "ipc/cup_messages.hpp"

#include <vsort/common/timestamp.hpp>

namespace vsort::service {

namespace fb = ipc::fb;

flatbuffers::Offset<fb::Cup> makeCup(flatbuffers::FlatBufferBuilder& fbb, const CupState& cup) {
    std::vector<flatbuffers::Offset<fb::CupCell>> cells;
    cells.reserve(cup.cells.size());
    for (const auto& cell : cup.cells) {
        flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<fb::MeasurementValue>>> values;
        if (!cell.measurements.empty()) {
            std::vector<flatbuffers::Offset<fb::MeasurementValue>> list;
            list.reserve(cell.measurements.size());
            for (const auto& m : cell.measurements) {
                list.push_back(fb::CreateMeasurementValue(fbb, fbb.CreateString(m.key), m.value));
            }
            values = fbb.CreateVector(list);
        }
        cells.push_back(fb::CreateCupCell(fbb, cell.sensorId,
                                          static_cast<fb::CellStatus>(cell.status), values));
    }
    return fb::CreateCup(fbb, cup.cupId, fbb.CreateVector(cells));
}

flatbuffers::Offset<fb::CupSnapshotReply> makeCupSnapshot(flatbuffers::FlatBufferBuilder& fbb,
                                                          const std::vector<LaneSnapshot>& lanes) {
    std::vector<flatbuffers::Offset<fb::LaneCups>> out;
    out.reserve(lanes.size());
    for (const auto& lane : lanes) {
        std::vector<flatbuffers::Offset<fb::Cup>> cups;
        cups.reserve(lane.cups.size());
        for (const auto& cup : lane.cups) {
            cups.push_back(makeCup(fbb, cup));
        }
        out.push_back(
            fb::CreateLaneCups(fbb, lane.laneId, lane.seq, lane.depth, fbb.CreateVector(cups)));
    }
    return fb::CreateCupSnapshotReply(fbb, fbb.CreateVector(out));
}

std::vector<std::uint8_t> makeCupUpdateEnvelope(const CupUpdate& update) {
    flatbuffers::FlatBufferBuilder fbb{512};
    std::vector<flatbuffers::Offset<fb::Cup>> cups;
    cups.reserve(update.cups.size());
    for (const auto& cup : update.cups) {
        cups.push_back(makeCup(fbb, cup));
    }
    const auto event =
        fb::CreateCupUpdateEvent(fbb, update.laneId, update.seq, fbb.CreateVector(cups));
    return ipc::finishEnvelope(fbb,
                               {.type = fb::MsgType::CupUpdate,
                                .requestId = 0,
                                .timestampNs = static_cast<std::uint64_t>(Timestamp::now().ns()),
                                .status = 0,
                                .errorText = {}},
                               fb::Payload::CupUpdateEvent, event.Union());
}

} // namespace vsort::service

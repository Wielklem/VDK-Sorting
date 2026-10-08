#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

namespace vsort::hmi {

// Product Monitor data as the service sends it (MSG-50-02, P80.100).
enum class CupCellStatus : std::uint8_t { Pending = 0, Ok = 1, NoData = 2 };

struct MeasurementData {
    QString key;
    double value{0.0};

    bool operator==(const MeasurementData&) const = default;
};

struct CupCellData {
    std::uint16_t sensorId{0};
    CupCellStatus status{CupCellStatus::Pending};
    QVector<MeasurementData> measurements{}; // P60.15: analysis of that sensor's photo

    bool operator==(const CupCellData&) const = default;
};

struct CupRowData {
    std::int64_t cupId{0};
    QVector<CupCellData> cells; // every sensor of the lane, upstream first

    bool operator==(const CupRowData&) const = default;
};

// A snapshot of one lane (GetCupSnapshot) or one CupUpdate event (depth 0: not sent).
struct LaneCupsData {
    std::uint16_t laneId{0};
    std::uint64_t seq{0};
    std::uint16_t depth{0};
    QVector<CupRowData> cups; // newest first
};

} // namespace vsort::hmi

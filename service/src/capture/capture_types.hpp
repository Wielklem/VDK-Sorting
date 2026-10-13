#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vsort/common/error.hpp>

#include "analysis/measurement.hpp"

namespace vsort::service {

// P60.100 Photo capture: saves photos of lane ROIs to a folder, to review how the analysis
// judged them (are the "empty" cups really empty?).

// Which photos are saved. Values follow the IPC schema (CaptureMode).
enum class CaptureMode : std::uint8_t {
    NoPotato = 1,  // count == 0
    Single = 2,    // count == 1
    Multiple = 3,  // count >= 2
    SizeRange = 4, // count >= 1 and length of the largest object within [minMm, maxMm]
    All = 5,       // every photo
};

// Values follow the IPC schema (CaptureState).
enum class CaptureState : std::uint8_t { Idle = 0, Running, Done, Stopped, Failed };

// What the analysis found in one lane ROI of one frame.
struct CaptureValues {
    std::uint32_t count{0};
    std::optional<double> lengthMm; // largest object; only when count >= 1
    std::optional<double> widthMm;
    std::optional<double> areaMm2;
};

[[nodiscard]] CaptureValues makeCaptureValues(const std::vector<MeasurementValue>& values);

struct CaptureFilter {
    CaptureMode mode{CaptureMode::NoPotato};
    double minMm{0.0}; // SizeRange only
    double maxMm{0.0};

    [[nodiscard]] bool accepts(const CaptureValues& values) const noexcept;
    // InvalidArgument: unknown mode, or a SizeRange that is negative, not finite or min > max.
    [[nodiscard]] Result<> validate() const;
};

// "empty", "single" or "multi": part of the photo file name.
[[nodiscard]] std::string_view verdictName(std::uint32_t count) noexcept;

// A sensor (lane ROI of a camera) that can be captured.
struct CaptureSensor {
    std::uint16_t sensorId{0};
    std::uint16_t cameraId{0};
    std::string name{};
    std::string laneName{};
};

struct CaptureRequest {
    std::filesystem::path folder; // absolute; a session folder is created below it
    std::uint32_t photosPerSensor{50};
    CaptureFilter filter;
    std::vector<std::uint16_t> sensorIds; // sensors to capture
};

struct CaptureSensorStatus {
    std::uint16_t sensorId{0};
    std::uint16_t cameraId{0};
    bool selected{false}; // part of the current or last session
    std::uint32_t saved{0};
    std::uint32_t target{0};
    std::string name{};
    std::string laneName{};
};

struct CaptureStatus {
    CaptureState state{CaptureState::Idle};
    std::filesystem::path sessionFolder; // empty before the first start
    std::filesystem::path defaultFolder;
    std::string errorText; // state Failed
    std::vector<CaptureSensorStatus> sensors;
};

// Paths travel as UTF-8 text on the IPC.
[[nodiscard]] std::string toUtf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text);

} // namespace vsort::service

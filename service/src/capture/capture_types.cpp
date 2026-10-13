#include "capture/capture_types.hpp"

#include <cmath>
#include <string>

namespace vsort::service {

namespace {

std::optional<double> find(const std::vector<MeasurementValue>& values, std::string_view key) {
    for (const auto& v : values) {
        if (v.key == key) {
            return v.value;
        }
    }
    return std::nullopt;
}

} // namespace

CaptureValues makeCaptureValues(const std::vector<MeasurementValue>& values) {
    CaptureValues out;
    if (const auto count = find(values, kKeyCount); count && *count > 0.0) {
        out.count = static_cast<std::uint32_t>(*count);
    }
    out.lengthMm = find(values, kKeyLength);
    out.widthMm = find(values, kKeyWidth);
    out.areaMm2 = find(values, kKeyArea);
    return out;
}

bool CaptureFilter::accepts(const CaptureValues& values) const noexcept {
    switch (mode) {
    case CaptureMode::NoPotato:
        return values.count == 0;
    case CaptureMode::Single:
        return values.count == 1;
    case CaptureMode::Multiple:
        return values.count >= 2;
    case CaptureMode::SizeRange:
        return values.count >= 1 && values.lengthMm && *values.lengthMm >= minMm &&
               *values.lengthMm <= maxMm;
    }
    return false;
}

Result<> CaptureFilter::validate() const {
    switch (mode) {
    case CaptureMode::NoPotato:
    case CaptureMode::Single:
    case CaptureMode::Multiple:
        return {};
    case CaptureMode::SizeRange:
        if (!std::isfinite(minMm) || !std::isfinite(maxMm) || minMm < 0.0 || minMm > maxMm) {
            return makeError(Errc::InvalidArgument,
                             "size range needs 0 <= min <= max (millimetres)");
        }
        return {};
    }
    return makeError(Errc::InvalidArgument, "unknown capture mode");
}

std::string_view verdictName(std::uint32_t count) noexcept {
    if (count == 0) {
        return "empty";
    }
    return count == 1 ? "single" : "multi";
}

std::string toUtf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string{reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path pathFromUtf8(std::string_view text) {
    return std::filesystem::path{
        std::u8string{reinterpret_cast<const char8_t*>(text.data()), text.size()}};
}

} // namespace vsort::service

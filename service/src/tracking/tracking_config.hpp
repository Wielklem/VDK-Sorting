#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

#include <vsort/common/config_store.hpp>
#include <vsort/common/error.hpp>

#include "tracking/frame_sequence_tracker.hpp"

namespace vsort::service {

// Config module "tracking" (P40.40): every threshold of the miss detection and the cross-sensor
// check. All are relative to measured intervals; none depends on the machine speed.
inline constexpr std::string_view kTrackingModule = "tracking";

[[nodiscard]] nlohmann::json trackingSchema();
[[nodiscard]] nlohmann::json trackingDefaults(); // = FrameSequenceOptions{}

[[nodiscard]] Result<FrameSequenceOptions> trackingOptionsFromJson(const nlohmann::json& config);
[[nodiscard]] nlohmann::json trackingOptionsToJson(const FrameSequenceOptions& options);

[[nodiscard]] Result<> registerTrackingConfig(IConfigStore& store);
// Defaults when the module is not registered (tests).
[[nodiscard]] Result<FrameSequenceOptions> loadTrackingOptions(const IConfigStore& store);

} // namespace vsort::service

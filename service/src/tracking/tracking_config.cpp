#include "tracking/tracking_config.hpp"

#include <chrono>
#include <string>

namespace vsort::service {

using nlohmann::json;

json trackingSchema() {
    return json::parse(R"({
      "type": "object",
      "additionalProperties": false,
      "required": ["max_gap_fill", "history_intervals", "miss_factor", "extra_factor",
                   "max_insert", "confirm_tolerance", "stable_intervals", "stable_tolerance",
                   "steady_tolerance",
                   "stop_timeout_min_ms", "stop_timeout_periods", "consistency_tolerance",
                   "jump_tolerance", "consistency_confirm", "baseline_rate", "relearn_after",
                   "mark_limit"],
      "properties": {
        "max_gap_fill": {"type": "integer", "minimum": 0, "maximum": 10000},
        "history_intervals": {"type": "integer", "minimum": 3, "maximum": 100},
        "miss_factor": {"type": "number", "minimum": 1.2, "maximum": 3.0},
        "extra_factor": {"type": "number", "minimum": 0.0, "maximum": 0.9},
        "max_insert": {"type": "integer", "minimum": 0, "maximum": 50},
        "confirm_tolerance": {"type": "number", "minimum": 0.05, "maximum": 0.5},
        "stable_intervals": {"type": "integer", "minimum": 2, "maximum": 50},
        "stable_tolerance": {"type": "number", "minimum": 0.01, "maximum": 0.5},
        "steady_tolerance": {"type": "number", "minimum": 0.01, "maximum": 0.5},
        "stop_timeout_min_ms": {"type": "integer", "minimum": 100, "maximum": 60000},
        "stop_timeout_periods": {"type": "number", "minimum": 2.0, "maximum": 50.0},
        "consistency_tolerance": {"type": "number", "minimum": 0.05, "maximum": 0.45},
        "jump_tolerance": {"type": "number", "minimum": 0.05, "maximum": 0.45},
        "consistency_confirm": {"type": "integer", "minimum": 1, "maximum": 20},
        "baseline_rate": {"type": "number", "minimum": 0.0, "maximum": 1.0},
        "relearn_after": {"type": "integer", "minimum": 2, "maximum": 1000},
        "mark_limit": {"type": "integer", "minimum": 0, "maximum": 1000}
      }
    })");
}

json trackingOptionsToJson(const FrameSequenceOptions& o) {
    return json{{"max_gap_fill", o.timing.maxGapFill},
                {"history_intervals", o.timing.history},
                {"miss_factor", o.timing.missFactor},
                {"extra_factor", o.timing.extraFactor},
                {"max_insert", o.timing.maxInsert},
                {"confirm_tolerance", o.timing.confirmTolerance},
                {"stable_intervals", o.timing.stableIntervals},
                {"stable_tolerance", o.timing.stableTolerance},
                {"steady_tolerance", o.timing.steadyTolerance},
                {"stop_timeout_min_ms", o.timing.stopTimeoutMin.count()},
                {"stop_timeout_periods", o.timing.stopTimeoutPeriods},
                {"consistency_tolerance", o.consistency.tolerance},
                {"jump_tolerance", o.consistency.jumpTolerance},
                {"consistency_confirm", o.consistency.confirm},
                {"baseline_rate", o.consistency.baselineRate},
                {"relearn_after", o.consistency.relearnAfter},
                {"mark_limit", o.markLimit}};
}

json trackingDefaults() {
    return trackingOptionsToJson(FrameSequenceOptions{});
}

Result<FrameSequenceOptions> trackingOptionsFromJson(const json& c) {
    if (auto valid = validateJson(trackingSchema(), c); !valid) {
        return std::unexpected{std::move(valid.error())};
    }
    FrameSequenceOptions o;
    o.timing.maxGapFill = c.at("max_gap_fill").get<std::uint64_t>();
    o.timing.history = c.at("history_intervals").get<std::uint32_t>();
    o.timing.missFactor = c.at("miss_factor").get<double>();
    o.timing.extraFactor = c.at("extra_factor").get<double>();
    o.timing.maxInsert = c.at("max_insert").get<std::uint32_t>();
    o.timing.confirmTolerance = c.at("confirm_tolerance").get<double>();
    o.timing.stableIntervals = c.at("stable_intervals").get<std::uint32_t>();
    o.timing.stableTolerance = c.at("stable_tolerance").get<double>();
    o.timing.steadyTolerance = c.at("steady_tolerance").get<double>();
    o.timing.stopTimeoutMin = std::chrono::milliseconds{c.at("stop_timeout_min_ms").get<int>()};
    o.timing.stopTimeoutPeriods = c.at("stop_timeout_periods").get<double>();
    o.consistency.tolerance = c.at("consistency_tolerance").get<double>();
    o.consistency.jumpTolerance = c.at("jump_tolerance").get<double>();
    o.consistency.confirm = c.at("consistency_confirm").get<std::uint32_t>();
    o.consistency.baselineRate = c.at("baseline_rate").get<double>();
    o.consistency.relearnAfter = c.at("relearn_after").get<std::uint32_t>();
    o.markLimit = c.at("mark_limit").get<std::uint32_t>();
    return o;
}

Result<> registerTrackingConfig(IConfigStore& store) {
    return store.registerModule(std::string{kTrackingModule}, trackingSchema(), trackingDefaults());
}

Result<FrameSequenceOptions> loadTrackingOptions(const IConfigStore& store) {
    const auto config = store.get(kTrackingModule);
    if (!config) {
        if (config.error().code == Errc::NotFound) {
            return FrameSequenceOptions{};
        }
        return std::unexpected{config.error()};
    }
    return trackingOptionsFromJson(*config);
}

} // namespace vsort::service

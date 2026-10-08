#include "cups/cup_monitor.hpp"

#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include <vsort/common/machine_config.hpp>

namespace vsort::service {

CupMonitor::CupMonitor(const IConfigStore* config, CupMonitorOptions options)
    : config_{config}
    , options_{options} {}

CupMonitor::~CupMonitor() {
    stop();
}

Result<> CupMonitor::init(ModuleContext& context) {
    if (config_ == nullptr) {
        return makeError(Errc::InvalidArgument, "cups: no config store");
    }
    auto machine = loadMachineConfig(*config_);
    if (!machine) {
        return std::unexpected{std::move(machine.error())};
    }
    bus_ = &context.bus;
    records_ = bus_->subscribe<ObjectRecord>(options_.queueCapacity);
    measurements_ = bus_->subscribe<Measurement>(options_.queueCapacity);
    const std::scoped_lock lock{mutex_};
    table_.emplace(*machine, options_.table);
    return {};
}

Result<> CupMonitor::start() {
    if (!table_ || bus_ == nullptr) {
        return makeError(Errc::InvalidArgument, "cups: start() before init()");
    }
    thread_ = std::jthread{[this](const std::stop_token& stop) { run(stop); }};
    return {};
}

void CupMonitor::stop() noexcept {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

Health CupMonitor::health() const {
    std::uint64_t ignored = 0;
    std::uint64_t ignoredMeasurements = 0;
    {
        const std::scoped_lock lock{mutex_};
        ignored = table_ ? table_->ignored() : 0;
        ignoredMeasurements = table_ ? table_->ignoredMeasurements() : 0;
    }
    const std::uint64_t dropped =
        (records_ ? records_->dropped() : 0) + (measurements_ ? measurements_->dropped() : 0);
    return Health{.state = dropped > 0 ? HealthState::Degraded : HealthState::Ok,
                  .detail = std::to_string(applied_.load(std::memory_order_relaxed)) +
                            " records, " + std::to_string(ignored) + " ignored, " +
                            std::to_string(measured_.load(std::memory_order_relaxed)) +
                            " measurements, " + std::to_string(ignoredMeasurements) + " ignored, " +
                            std::to_string(dropped) + " dropped on the bus"};
}

std::vector<LaneSnapshot> CupMonitor::snapshot(std::optional<std::uint16_t> laneId) const {
    const std::scoped_lock lock{mutex_};
    return table_ ? table_->snapshot(laneId) : std::vector<LaneSnapshot>{};
}

void CupMonitor::flush() {
    if (!records_ || !measurements_ || bus_ == nullptr) {
        return;
    }
    std::vector<CupUpdate> updates;
    std::vector<Measurement> measurements;
    {
        const std::scoped_lock lock{mutex_};
        const auto applyRecord = [this](const ObjectRecord& r) {
            table_->apply(r);
            applied_.fetch_add(1, std::memory_order_relaxed);
        };
        records_->drain(applyRecord);
        measurements_->drain([&](const Measurement& m) { measurements.push_back(m); });
        // A Measurement is published after its record: drain again so those records are in.
        records_->drain(applyRecord);
        for (const auto& m : measurements) {
            table_->apply(m);
        }
        measured_.fetch_add(measurements.size(), std::memory_order_relaxed);
        updates = table_->takeUpdates();
    }
    for (const auto& update : updates) {
        bus_->publish(update);
    }
}

void CupMonitor::logSummary() {
    std::string line;
    for (const auto& lane : snapshot(std::nullopt)) {
        std::size_t ok = 0;
        std::size_t noData = 0;
        std::size_t pending = 0;
        for (const auto& cup : lane.cups) {
            for (const auto& cell : cup.cells) {
                ok += cell.status == CellStatus::Ok ? 1U : 0U;
                noData += cell.status == CellStatus::NoData ? 1U : 0U;
                pending += cell.status == CellStatus::Pending ? 1U : 0U;
            }
        }
        line += " | lane " + std::to_string(lane.laneId) + ": " + std::to_string(lane.cups.size()) +
                " cups";
        if (!lane.cups.empty()) {
            line += " (newest " + std::to_string(lane.cups.front().cupId) + ")";
        }
        line += ", cells " + std::to_string(ok) + " ok, " + std::to_string(noData) + " no data, " +
                std::to_string(pending) + " pending";
    }
    if (line != lastSummary_) {
        spdlog::info("cups{}", line);
        lastSummary_ = std::move(line);
    }
}

void CupMonitor::run(const std::stop_token& stop) {
    auto nextLog = std::chrono::steady_clock::now() + options_.logInterval;
    while (!stop.stop_requested()) {
        flush();
        if (std::chrono::steady_clock::now() >= nextLog) {
            logSummary();
            nextLog += options_.logInterval;
        }
        std::this_thread::sleep_for(options_.interval);
    }
}

} // namespace vsort::service

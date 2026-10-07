#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <vsort/common/config_store.hpp>
#include <vsort/common/message_bus.hpp>
#include <vsort/common/module.hpp>

#include "cups/cup_table.hpp"
#include "cups/cup_types.hpp"
#include "tracking/object_record.hpp"

namespace vsort::service {

struct CupMonitorOptions {
    CupTableOptions table;
    std::chrono::milliseconds interval{100}; // updates are batched: at most 10 per second
    std::size_t queueCapacity{4096};         // ObjectRecords waiting on the bus
    std::chrono::seconds logInterval{10};    // summary per lane in the log, when it changed
};

// IModule "cups" (P80.100): keeps the last cups per lane (CupTable) from the ObjectRecords on the
// bus and publishes CupUpdate (MSG-50-02) on the bus at most every `interval`. The IPC server
// sends those to the HMI and answers snapshot requests through ICupSource.
// Reads the machine config at init; restart the service after editing it.
class CupMonitor final : public IModule, public ICupSource {
public:
    // `config` must outlive the module.
    explicit CupMonitor(const IConfigStore* config, CupMonitorOptions options = {});
    ~CupMonitor() override;
    CupMonitor(const CupMonitor&) = delete;
    CupMonitor& operator=(const CupMonitor&) = delete;
    CupMonitor(CupMonitor&&) = delete;
    CupMonitor& operator=(CupMonitor&&) = delete;

    [[nodiscard]] std::string_view name() const noexcept override { return "cups"; }
    [[nodiscard]] Result<> init(ModuleContext& context) override;
    [[nodiscard]] Result<> start() override;
    void stop() noexcept override;
    [[nodiscard]] Health health() const override;

    [[nodiscard]] std::vector<LaneSnapshot>
    snapshot(std::optional<std::uint16_t> laneId) const override;

    // Applies waiting records and publishes the updates now (also used by the thread).
    void flush();

private:
    void run(const std::stop_token& stop);
    void logSummary();

    const IConfigStore* config_{nullptr};
    CupMonitorOptions options_;
    MessageBus* bus_{nullptr};
    std::shared_ptr<Subscription<ObjectRecord>> records_;
    mutable std::mutex mutex_;
    std::optional<CupTable> table_; // guarded by mutex_
    std::jthread thread_;
    std::atomic<std::uint64_t> applied_{0};
    std::string lastSummary_; // monitor thread only
};

} // namespace vsort::service

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <vsort/common/error.hpp>
#include <vsort/common/message_bus.hpp>

namespace vsort {

enum class HealthState : std::uint8_t { Unknown, Ok, Degraded, Failed };

struct Health {
    HealthState state{HealthState::Unknown};
    std::string detail;
};

enum class ModuleState : std::uint8_t { Created, Initialized, Running, Stopped, Failed };

// Shared services handed to every module in init(). Extended later (config store, platform).
struct ModuleContext {
    MessageBus& bus;
};

// A unit of the vision service (camera, detector, sorter, ...).
// Lifecycle: init() -> start() -> stop(). Driven by ModuleRegistry from the main thread.
class IModule {
public:
    IModule() = default;
    virtual ~IModule() = default;
    IModule(const IModule&) = delete;
    IModule& operator=(const IModule&) = delete;
    IModule(IModule&&) = delete;
    IModule& operator=(IModule&&) = delete;

    // Unique, stable, non-empty. Also used as logger name.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // Names of modules that must be initialized and started before this one.
    [[nodiscard]] virtual std::vector<std::string> dependencies() const { return {}; }

    // Acquire resources, subscribe to the bus. No threads running yet.
    [[nodiscard]] virtual Result<> init(ModuleContext& context) = 0;

    // Start worker threads / processing.
    [[nodiscard]] virtual Result<> start() = 0;

    // Stop threads and release resources. Must be safe after init() without start(),
    // and after a failed start(). Must not throw.
    virtual void stop() noexcept = 0;

    // Thread-safe, cheap, non-blocking. Polled by the watchdog.
    [[nodiscard]] virtual Health health() const = 0;
};

} // namespace vsort

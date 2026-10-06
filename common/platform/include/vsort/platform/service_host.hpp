#pragma once

#include <chrono>
#include <memory>

#include <vsort/common/error.hpp>
#include <vsort/platform/interface.hpp>

namespace vsort::platform {

// Process lifecycle towards the OS service manager (systemd now, Windows SCM later).
class IServiceHost : public Interface {
public:
    // Call once from main() before starting any other thread. Linux: SIGTERM and SIGINT.
    // A second stop signal while stopping exits the process immediately.
    [[nodiscard]] virtual Result<> installStopHandlers() = 0;

    [[nodiscard]] virtual bool stopRequested() const noexcept = 0;
    virtual void requestStop() noexcept = 0;  // programmatic stop, e.g. after a fatal module error
    // true when a stop was requested, false on timeout.
    [[nodiscard]] virtual bool waitForStop(std::chrono::milliseconds timeout) = 0;

    // Service manager notifications; no-ops when not running under one.
    virtual void notifyReady() noexcept = 0;
    virtual void notifyStopping() noexcept = 0;
    virtual void notifyWatchdog() noexcept = 0;
    [[nodiscard]] virtual bool underServiceManager() const noexcept = 0;
};

[[nodiscard]] Result<std::unique_ptr<IServiceHost>> makeServiceHost();

} // namespace vsort::platform

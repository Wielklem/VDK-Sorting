#include "service_app.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <string_view>

#include <opencv2/core.hpp>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <zmq.hpp>

#include <vsort/common/config_store.hpp>
#include <vsort/common/logging.hpp>
#include <vsort/common/message_bus.hpp>
#include <vsort/common/module_registry.hpp>
#include <vsort/common/roi_config.hpp>
#include <vsort/common/version.hpp>
#include <vsort/platform/paths.hpp>

#include "ipc/ipc_server.hpp"

namespace vsort::service {
namespace {

using namespace std::chrono_literals;

// Before logging is up, errors go to stderr.
int fail(std::string_view what, const Error& error) {
    std::cerr << "vsort_service: " << what << ": " << error.what() << '\n';
    return kExitFailure;
}

// Polls module health. A Failed module means the service cannot do its job: stop.
bool anyModuleFailed(const ModuleRegistry& registry) {
    bool failed = false;
    for (const auto& entry : registry.status()) {
        if (entry.health.state == HealthState::Failed) {
            spdlog::critical("module '{}' failed: {}", entry.name, entry.health.detail);
            failed = true;
        }
    }
    return failed;
}

} // namespace

int runService(const Options& options, platform::IServiceHost& host) {
    // 1. Paths
    std::unique_ptr<platform::IPaths> paths;
    if (options.root.empty()) {
        auto standard = platform::makeStandardPaths(options.scope);
        if (!standard) {
            return fail("cannot determine standard paths", standard.error());
        }
        paths = std::move(*standard);
    } else {
        paths = platform::makeRootedPaths(options.root);
    }
    if (const auto dirs = platform::ensureDirectories(*paths); !dirs) {
        std::cerr << "hint: use --user or --root <dir> when not running as a system service\n";
        return fail("cannot create directories", dirs.error());
    }

    // 2. Logging (rotating file in logDir)
    const log::Config logConfig{
        .directory = paths->logDir(), .level = options.logLevel, .console = !options.noConsole};
    if (const auto logging = log::init(logConfig); !logging) {
        return fail("cannot start logging", logging.error());
    }

    spdlog::info("vsort_service {} starting", vsort::version());
    spdlog::info("config={} data={} log={}", paths->configDir().string(), paths->dataDir().string(),
                 paths->logDir().string());
    spdlog::info("OpenCV {}, SQLite {}", CV_VERSION, sqlite3_libversion());
    const auto [major, minor, patch] = zmq::version();
    spdlog::info("ZeroMQ {}.{}.{}", major, minor, patch);

    // 3. Start modules (dependency order; on failure the registry stops what it started)
    MessageBus bus;
    FileConfigStore configStore{paths->configDir(), bus};
    ModuleContext context{bus};
    if (const auto roiConfig = registerRoiConfig(configStore); !roiConfig) {
        spdlog::critical("cannot register ROI config: {}", roiConfig.error().what());
        log::shutdown();
        return kExitFailure;
    }
    ModuleRegistry registry; // declared after bus and configStore: destroyed before them
    // Modules are added here as they are implemented: registry.add(std::make_unique<...>());
    // The camera module will call IpcServer::preview().submit() and replace NullCameraAccess.
    if (const auto added = registry.add(std::make_unique<IpcServer>(
            IpcConfig{}, std::make_unique<NullCameraAccess>(), &configStore));
        !added) {
        spdlog::critical("cannot add ipc module: {}", added.error().what());
        log::shutdown();
        return kExitFailure;
    }
    if (const auto started = registry.startAll(context); !started) {
        spdlog::critical("module start failed: {}", started.error().what());
        log::shutdown();
        return kExitFailure;
    }

    int exitCode = kExitOk;
    if (options.check) {
        spdlog::info("--check: startup OK, exiting");
    } else {
        host.notifyReady();
        spdlog::info("running; SIGTERM/SIGINT to stop");
        // Wake once per second: polls module health and feeds the systemd watchdog
        // (WatchdogSec must be > 1 s). A failed module triggers a controlled stop.
        while (!host.waitForStop(1s)) {
            if (anyModuleFailed(registry)) {
                exitCode = kExitFailure;
                host.requestStop();
                break;
            }
            host.notifyWatchdog();
        }
        spdlog::info("stop requested");
    }

    // 4. Graceful shutdown: modules in reverse start order, then the logger
    host.notifyStopping();
    registry.stopAll();
    spdlog::info("message bus dropped {} messages in total", bus.droppedTotal());
    spdlog::info("vsort_service stopped (exit code {})", exitCode);
    log::shutdown();
    return exitCode;
}

} // namespace vsort::service

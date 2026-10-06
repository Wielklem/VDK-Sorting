#include "service_app.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <string_view>

#include <opencv2/core.hpp>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <zmq.hpp>

#include <vsort/common/logging.hpp>
#include <vsort/common/version.hpp>
#include <vsort/platform/paths.hpp>

namespace vsort::service {
namespace {

using namespace std::chrono_literals;

// Before logging is up, errors go to stderr.
int fail(std::string_view what, const Error& error) {
    std::cerr << "vsort_service: " << what << ": " << error.what() << '\n';
    return kExitFailure;
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
    const log::Config logConfig{.directory = paths->logDir(),
                                .level = options.logLevel,
                                .console = !options.noConsole};
    if (const auto logging = log::init(logConfig); !logging) {
        return fail("cannot start logging", logging.error());
    }

    spdlog::info("vsort_service {} starting", vsort::version());
    spdlog::info("config={} data={} log={}", paths->configDir().string(), paths->dataDir().string(),
                 paths->logDir().string());
    spdlog::info("OpenCV {}, SQLite {}", CV_VERSION, sqlite3_libversion());
    const auto [major, minor, patch] = zmq::version();
    spdlog::info("ZeroMQ {}.{}.{}", major, minor, patch);

    // 3. Start modules  <-- P10.60 hook: registry.initAll(), registry.startAll()

    if (options.check) {
        spdlog::info("--check: startup OK, exiting");
    } else {
        host.notifyReady();
        spdlog::info("running; SIGTERM/SIGINT to stop");
        // Wake once per second: also feeds the systemd watchdog (WatchdogSec must be > 1 s).
        while (!host.waitForStop(1s)) {
            host.notifyWatchdog();
        }
        spdlog::info("stop requested");
    }

    // 4. Graceful shutdown (reverse order)
    host.notifyStopping();
    // P10.60 hook: registry.stopAll()  (reverse start order)
    spdlog::info("vsort_service stopped");
    log::shutdown();
    return kExitOk;
}

} // namespace vsort::service

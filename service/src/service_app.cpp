#include "service_app.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <string_view>

#include <opencv2/core.hpp>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <zmq.hpp>

#include <vsort/camera/camera_map.hpp>
#include <vsort/camera/camera_settings_config.hpp>
#include <vsort/common/config_store.hpp>
#include <vsort/common/logging.hpp>
#include <vsort/common/message_bus.hpp>
#include <vsort/common/module_registry.hpp>
#include <vsort/common/machine_config.hpp>
#include <vsort/common/roi_config.hpp>
#include <vsort/common/version.hpp>
#include <vsort/platform/paths.hpp>

#include "analysis/analysis_config.hpp"
#include "analysis/analysis_module.hpp"
#include "camera/camera_manager.hpp"
#include "camera/camera_module.hpp"
#include "camera/replay_backend.hpp"
#include "cups/cup_monitor.hpp"
#include "ipc/ipc_server.hpp"
#include "tracking/tracking_config.hpp"
#include "tracking/tracking_module.hpp"
#ifdef VSORT_WITH_GALAXY
#include "camera/daheng_backend.hpp"
#endif

namespace vsort::service {
namespace {

using namespace std::chrono_literals;

// Before logging is up, errors go to stderr.
int fail(std::string_view what, const Error& error) {
    std::cerr << "vsort_service: " << what << ": " << error.what() << '\n';
    return kExitFailure;
}

// nullptr = no cameras (--camera-source none, or no Daheng support in this build).
Result<std::unique_ptr<ICameraBackend>> makeBackend(const Options& options) {
    CameraSource source = options.cameraSource;
    if (source == CameraSource::Auto) {
#ifdef VSORT_WITH_GALAXY
        source = CameraSource::Daheng;
#else
        source = CameraSource::None;
#endif
    }
    switch (source) {
    case CameraSource::Replay: {
        auto backend = ReplayBackend::create({.sessionDir = options.replayDir,
                                              .speed = options.replaySpeed,
                                              .loop = options.replayLoop});
        if (!backend) {
            return std::unexpected{backend.error()};
        }
        return std::unique_ptr<ICameraBackend>{std::move(*backend)};
    }
    case CameraSource::Daheng:
#ifdef VSORT_WITH_GALAXY
        return std::unique_ptr<ICameraBackend>{std::make_unique<DahengBackend>()};
#else
        return makeError(Errc::NotSupported,
                         "this build has no Daheng support (configure with VSORT_WITH_GALAXY)");
#endif
    case CameraSource::Auto:
    case CameraSource::None:
        break;
    }
    return std::unique_ptr<ICameraBackend>{};
}

Result<> registerCameraConfig(IConfigStore& store) {
    if (auto registered = camera::registerCameraMap(store); !registered) {
        return registered;
    }
    return camera::registerCameraSettings(store);
}

// P50.10: the service does not start with an invalid machine model. A ROI that is not drawn
// yet is only a warning.
Result<> setUpMachineConfig(IConfigStore& store) {
    if (auto registered = registerMachineConfig(store); !registered) {
        return registered;
    }
    const auto machine = loadMachineConfig(store);
    if (!machine) {
        return std::unexpected{machine.error()};
    }
    const auto lanes = machine->lanes();
    std::size_t sensors = 0;
    for (const auto* lane : lanes) {
        sensors += lane->sensors.size();
    }
    spdlog::info("machine: {} line(s), {} lane(s), {} sensor(s)", machine->lines().size(),
                 lanes.size(), sensors);
    if (const auto rois = store.get(kRoiModule); rois) {
        if (const auto refs = checkRoiReferences(*machine, *rois); !refs) {
            spdlog::warn("machine config: {}", refs.error().message);
        }
    }
    return {};
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
    if (const auto machineConfig = setUpMachineConfig(configStore); !machineConfig) {
        spdlog::critical("cannot load machine config: {}", machineConfig.error().what());
        log::shutdown();
        return kExitFailure;
    }
    if (const auto trackingConfig = registerTrackingConfig(configStore); !trackingConfig) {
        spdlog::critical("cannot register tracking config: {}", trackingConfig.error().what());
        log::shutdown();
        return kExitFailure;
    }
    if (const auto analysisConfig = registerAnalysisConfig(configStore); !analysisConfig) {
        spdlog::critical("cannot register analysis config: {}", analysisConfig.error().what());
        log::shutdown();
        return kExitFailure;
    }
    ModuleRegistry registry; // declared after bus and configStore: destroyed before them
    // Modules are added here as they are implemented: registry.add(std::make_unique<...>());
    // Cameras (P30.85): the manager is shared by the IPC server (as ICameraAccess) and the
    // camera module. Declared after the registry's dependencies, destroyed before the registry.
    std::shared_ptr<CameraManager> cameras;
    std::unique_ptr<ICameraAccess> cameraAccess = std::make_unique<NullCameraAccess>();
    auto backend = makeBackend(options);
    if (!backend) {
        spdlog::critical("cannot set up cameras: {}", backend.error().what());
        log::shutdown();
        return kExitFailure;
    }
    if (*backend) {
        if (const auto cameraConfig = registerCameraConfig(configStore); !cameraConfig) {
            spdlog::critical("cannot register camera config: {}", cameraConfig.error().what());
            log::shutdown();
            return kExitFailure;
        }
        cameras =
            std::make_shared<CameraManager>(std::move(*backend), &configStore,
                                            CameraManagerOptions{.forceFreeRun = options.freeRun});
        cameraAccess = std::make_unique<ManagerCameraAccess>(cameras);
    } else {
        spdlog::info("cameras: no camera source (use --camera-source or --replay)");
    }

    if (const auto added = registry.add(
            std::make_unique<IpcServer>(IpcConfig{}, std::move(cameraAccess), &configStore));
        !added) {
        spdlog::critical("cannot add ipc module: {}", added.error().what());
        log::shutdown();
        return kExitFailure;
    }
    // Product Monitor data (P80.100): last cups per lane, also without cameras (empty table).
    auto cupMonitor = std::make_unique<CupMonitor>(&configStore);
    static_cast<IpcServer*>(registry.find("ipc"))->setCupSource(cupMonitor.get());
    if (const auto added = registry.add(std::move(cupMonitor)); !added) {
        spdlog::critical("cannot add cups module: {}", added.error().what());
        log::shutdown();
        return kExitFailure;
    }
    if (cameras) {
        auto* ipc = static_cast<IpcServer*>(registry.find("ipc"));
        cameras->addFrameSink([ipc](const camera::Frame& frame) { ipc->preview().submit(frame); });
        cameras->setOnListChanged([ipc] { ipc->notifyCamerasChanged(); });
        if (const auto added = registry.add(std::make_unique<CameraModule>(cameras)); !added) {
            spdlog::critical("cannot add camera module: {}", added.error().what());
            log::shutdown();
            return kExitFailure;
        }
        // Tracking (P40.30): a second frame sink. It only enqueues metadata, so it stays fast.
        auto tracking = std::make_unique<TrackingModule>(
            &configStore, TrackingOptions{.stateFile = paths->dataDir() / "tracking_state.json"});
        TrackingModule* trackingModule = tracking.get();
        if (const auto added = registry.add(std::move(tracking)); !added) {
            spdlog::critical("cannot add tracking module: {}", added.error().what());
            log::shutdown();
            return kExitFailure;
        }
        cameras->addFrameSink(
            [trackingModule](const camera::Frame& frame) { trackingModule->submit(frame); });
        // Analysis (P60.10): a third frame sink. It queues the frame (never blocks) and releases
        // the buffer as soon as the pipeline is done.
        auto analysis = std::make_unique<AnalysisModule>(
            &configStore, AnalysisOptions{.debugDir = paths->dataDir() / "analysis_debug"});
        AnalysisModule* analysisModule = analysis.get();
        if (const auto added = registry.add(std::move(analysis)); !added) {
            spdlog::critical("cannot add analysis module: {}", added.error().what());
            log::shutdown();
            return kExitFailure;
        }
        cameras->addFrameSink(
            [analysisModule](const camera::Frame& frame) { analysisModule->submit(frame); });
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

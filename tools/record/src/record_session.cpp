#include "record_session.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <format>
#include <memory>
#include <ostream>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <vsort/camera/camera_settings_config.hpp>
#include <vsort/common/config_store.hpp>
#include <vsort/common/message_bus.hpp>
#include <vsort/platform/paths.hpp>

namespace vsort::record {
namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kPollInterval = std::chrono::milliseconds{20};
constexpr auto kReportInterval = std::chrono::seconds{1};
constexpr std::size_t kPoolHeadroom = 8;

struct Entry {
    CameraSpec spec;
    std::unique_ptr<camera::IMonitoredCamera> camera;
};

// Stops and closes every camera when it goes out of scope, on every exit path.
struct CameraSet {
    std::vector<Entry> entries;

    CameraSet() = default;
    CameraSet(const CameraSet&) = delete;
    CameraSet& operator=(const CameraSet&) = delete;
    CameraSet(CameraSet&&) = delete;
    CameraSet& operator=(CameraSet&&) = delete;
    ~CameraSet() { shutdown(); }

    void stopAll() noexcept {
        for (auto& entry : entries) {
            entry.camera->stop();
        }
    }

    void shutdown() noexcept {
        stopAll();
        for (auto& entry : entries) {
            entry.camera->close();
        }
    }
};

std::unexpected<Error> failed(const CameraSpec& spec, std::string_view step, const Error& error) {
    return makeError(error.code, std::format("camera {} ({}): {}: {}", spec.index, spec.serial,
                                             step, error.message));
}

// Shortcut (P20.95): the applied settings are only kept as text in the session label, not as
// fields of session.json; enough to see later how a dataset was recorded.
std::string describeLabel(const RecordPlan& plan) {
    std::string settings;
    for (const auto& spec : plan.cameras) {
        if (!settings.empty()) {
            settings += "; ";
        }
        settings += std::format("cam{}: {}", spec.index, describeSettings(spec.settings));
    }
    return plan.label.empty() ? settings : std::format("{} [{}]", plan.label, settings);
}

bool everyCameraReached(const CameraSet& set, std::uint64_t frames) {
    return std::ranges::all_of(set.entries, [frames](const Entry& entry) {
        return entry.camera->health().framesReceived >= frames;
    });
}

void printProgress(std::ostream& out, const CameraSet& set, Clock::duration elapsed) {
    out << std::format("[{:>4} s]",
                       std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
    for (const auto& entry : set.entries) {
        const camera::CameraHealth health = entry.camera->health();
        out << std::format("  cam{}: {} frames, {} dropped, {} missing", entry.spec.index,
                           health.framesReceived, health.framesDropped, health.framesMissed);
    }
    out << '\n';
    out.flush(); // live display
}

} // namespace

bool RecordReport::clean() const noexcept {
    if (stats.dropped != 0 || stats.failed != 0 || cameras.empty()) {
        return false;
    }
    return std::ranges::all_of(cameras, [](const CameraResult& cam) {
        return cam.health.framesReceived > 0 && cam.health.framesDropped == 0 &&
               cam.health.framesMissed == 0;
    });
}

std::size_t framePoolSize(std::size_t queueDepth) noexcept {
    return std::bit_ceil(std::max<std::size_t>(queueDepth, 1)) + kPoolHeadroom;
}

Result<SavedSettings> loadSavedSettings(const std::filesystem::path& root) {
    const auto paths = platform::makeRootedPaths(root);
    const auto configDir = paths->configDir();
    std::error_code ec;
    if (!std::filesystem::is_directory(configDir, ec)) {
        return makeError(Errc::NotFound,
                         std::format("no config folder '{}' (wrong --root?)", configDir.string()));
    }
    MessageBus bus; // the store publishes changes; nobody listens here
    FileConfigStore store{configDir, bus};
    if (const auto r = camera::registerCameraSettings(store); !r) {
        return std::unexpected{r.error()};
    }
    return camera::loadCameraSettings(store);
}

std::string describeSettings(const camera::CameraSettings& settings) {
    std::string trigger;
    switch (settings.triggerMode) {
    case camera::TriggerMode::FreeRun:
        trigger = "freerun";
        break;
    case camera::TriggerMode::Software:
        trigger = "software";
        break;
    case camera::TriggerMode::Hardware:
        trigger = settings.triggerEdge == camera::TriggerEdge::Falling ? "hardware (falling)"
                                                                       : "hardware (rising)";
        break;
    }
    const auto& roi = settings.roi;
    const std::string area =
        roi.width == 0 || roi.height == 0
            ? std::string{"full"}
            : std::format("{}x{} at {},{}", roi.width, roi.height, roi.x, roi.y);
    return std::format("exposure {} us, gain {} dB, trigger {}, roi {}", settings.exposureUs,
                       settings.gainDb, trigger, area);
}

Result<RecordPlan> makePlan(const Options& options, const SavedSettings* saved) {
    RecordPlan plan;
    plan.outDir = options.outDir;
    plan.label = options.label;
    plan.cameras = options.cameras;
    for (auto& spec : plan.cameras) {
        spec.settings = camera::CameraSettings{};
        if (saved != nullptr) {
            const auto it = saved->find(spec.index);
            if (it == saved->end()) {
                return makeError(Errc::InvalidArgument,
                                 std::format("camera {}: no saved settings in camera_settings "
                                             "(set them in the HMI first, or leave out --root)",
                                             spec.index));
            }
            spec.settings = it->second;
        }
        if (options.exposureUs) {
            spec.settings.exposureUs = *options.exposureUs;
        }
        if (options.gainDb) {
            spec.settings.gainDb = *options.gainDb;
        }
        if (options.trigger) {
            spec.settings.triggerMode = *options.trigger;
        }
        if (spec.settings.triggerMode == camera::TriggerMode::Software) {
            return makeError(Errc::InvalidArgument,
                             std::format("camera {}: software trigger cannot be recorded "
                                         "(pass --trigger hardware or --trigger freerun)",
                                         spec.index));
        }
    }
    plan.duration = std::chrono::duration_cast<std::chrono::milliseconds>(options.duration);
    plan.framesPerCamera = options.framesPerCamera;
    plan.queueDepth = options.queueDepth;
    return plan;
}

Result<RecordReport> runRecording(const RecordPlan& plan, const camera::CameraFactory& factory,
                                  const StopCheck& stopRequested, std::ostream& out) {
    if (plan.cameras.empty()) {
        return makeError(Errc::InvalidArgument, "no cameras to record");
    }
    if (plan.outDir.empty()) {
        return makeError(Errc::InvalidArgument, "output folder is empty");
    }
    if (!factory) {
        return makeError(Errc::InvalidArgument, "no camera factory");
    }

    // Declared before the cameras, so it is destroyed after them: the recorder must outlive
    // the streaming.
    std::unique_ptr<recorder::Recorder> recorder;
    CameraSet set;

    std::vector<recorder::CameraStream> streams;
    for (const auto& spec : plan.cameras) {
        set.entries.push_back(Entry{.spec = spec, .camera = camera::makeResilientCamera(factory)});
        camera::IMonitoredCamera& cam = *set.entries.back().camera;
        if (const auto r = cam.open(spec.serial); !r) {
            return failed(spec, "open", r.error());
        }
        if (const auto r = cam.applySettings(spec.settings); !r) {
            return failed(spec, "apply settings", r.error());
        }
        const auto info = cam.info();
        if (!info) {
            return failed(spec, "read info", info.error());
        }
        streams.push_back(recorder::CameraStream{
            .cameraIndex = spec.index, .serial = spec.serial, .model = info->model});
    }

    const recorder::RecorderConfig config{
        .rootDir = plan.outDir, .label = describeLabel(plan), .queueDepth = plan.queueDepth};
    auto started = recorder::Recorder::start(config, std::move(streams));
    if (!started) {
        return std::unexpected{std::move(started.error())};
    }
    recorder = std::move(*started);

    for (auto& entry : set.entries) {
        entry.camera->setFrameCallback(recorder->callbackFor(entry.spec.index));
    }
    for (auto& entry : set.entries) {
        if (const auto r = entry.camera->start(); !r) {
            return failed(entry.spec, "start", r.error());
        }
    }

    out << std::format("recording to {}\n", recorder->sessionDir().string());
    const auto begin = Clock::now();
    auto nextReport = begin + kReportInterval;
    for (;;) {
        if (stopRequested && stopRequested()) {
            break;
        }
        const auto now = Clock::now();
        if (plan.duration.count() > 0 && now - begin >= plan.duration) {
            break;
        }
        if (plan.framesPerCamera > 0 && everyCameraReached(set, plan.framesPerCamera)) {
            break;
        }
        if (now >= nextReport) {
            printProgress(out, set, now - begin);
            nextReport += kReportInterval;
        }
        std::this_thread::sleep_for(kPollInterval);
    }

    // Cameras first (no more callbacks), then the recorder drains its queue and closes the session.
    set.stopAll();
    RecordReport report;
    report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin);
    for (const auto& entry : set.entries) {
        report.cameras.push_back(CameraResult{.index = entry.spec.index,
                                              .serial = entry.spec.serial,
                                              .health = entry.camera->health()});
    }
    const auto finished = recorder->finish();
    report.sessionDir = recorder->sessionDir();
    report.stats = recorder->stats();
    if (!finished) {
        return std::unexpected{finished.error()};
    }
    return report;
}

} // namespace vsort::record

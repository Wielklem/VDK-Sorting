#include "tracking/tracking_module.hpp"

#include <array>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#include <spdlog/spdlog.h>

#include <vsort/common/machine_config.hpp>
#include <vsort/common/roi_config.hpp>

#include "tracking/frame_sequence_tracker.hpp"
#include "tracking/tracking_config.hpp"

namespace vsort::service {

using namespace std::chrono_literals;

namespace {
constexpr auto kDegradedAfterDrop = 10s;
constexpr std::size_t kMaxFramesPerLoop = 64;
} // namespace

bool FrameInbox::push(const camera::FrameMetadata& meta) noexcept {
    if (!open_.load(std::memory_order_acquire)) {
        return false;
    }
    if (!queue_.tryPush(meta)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    accepted_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

TrackingModule::TrackingModule(const IConfigStore* config, TrackingOptions options)
    : config_{config}
    , options_{options}
    , inbox_{options.queueCapacity} {}

TrackingModule::TrackingModule(std::unique_ptr<ITracker> tracker, TrackingOptions options)
    : options_{options}
    , tracker_{std::move(tracker)}
    , inbox_{options.queueCapacity} {}

TrackingModule::~TrackingModule() {
    stop();
}

void TrackingModule::loadState() {
    std::error_code ec;
    if (options_.stateFile.empty() || !std::filesystem::exists(options_.stateFile, ec)) {
        return;
    }
    std::ifstream in{options_.stateFile, std::ios::binary};
    const auto state = nlohmann::json::parse(in, nullptr, false);
    if (state.is_discarded()) {
        spdlog::warn("tracking: ignoring unreadable {}", options_.stateFile.generic_string());
        return;
    }
    tracker_->restoreState(state);
}

// Write to <file>.tmp, then rename: a crash never leaves a half-written file.
void TrackingModule::saveState() const {
    if (options_.stateFile.empty() || !tracker_) {
        return;
    }
    auto tmp = options_.stateFile;
    tmp += ".tmp";
    {
        std::ofstream out{tmp, std::ios::binary | std::ios::trunc};
        out << tracker_->state().dump(2) << '\n';
        if (!out) {
            spdlog::warn("tracking: cannot write {}", tmp.generic_string());
            return;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, options_.stateFile, ec);
    if (ec) {
        spdlog::warn("tracking: cannot save state: {}", ec.message());
    }
}

Result<> TrackingModule::init(ModuleContext& context) {
    bus_ = &context.bus;
    if (tracker_) {
        loadState();
        return {};
    }
    if (config_ == nullptr) {
        return makeError(Errc::InvalidArgument, "tracking: no config store and no tracker");
    }
    auto machine = loadMachineConfig(*config_);
    if (!machine) {
        return std::unexpected{std::move(machine.error())};
    }
    auto options = loadTrackingOptions(*config_);
    if (!options) {
        return std::unexpected{std::move(options.error())};
    }
    const auto rois = config_->get(kRoiModule);
    auto sensors = makeTrackedSensors(*machine, rois ? *rois : nlohmann::json::object());
    spdlog::info("tracking: {} sensor(s), queue {} frames", sensors.size(), inbox_.capacity());
    tracker_ = std::make_unique<FrameSequenceTracker>(std::move(sensors), *options);
    loadState();
    return {};
}

Result<> TrackingModule::start() {
    if (!tracker_ || bus_ == nullptr) {
        return makeError(Errc::InvalidArgument, "tracking: start() before init()");
    }
    inbox_.open();
    thread_ = std::jthread{[this](const std::stop_token& stop) { run(stop); }};
    return {};
}

void TrackingModule::stop() noexcept {
    inbox_.close();
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
        try {
            saveState();
        } catch (...) { // NOLINT(bugprone-empty-catch): stop() must not throw
        }
    }
}

Health TrackingModule::health() const {
    const auto dropped = inbox_.dropped();
    std::string detail = std::to_string(processed_.load(std::memory_order_relaxed)) + " frames, " +
                         std::to_string(dropped) + " dropped (queue full)";
    const auto lastDrop = lastDropNs_.load(std::memory_order_relaxed);
    const bool recentDrop =
        lastDrop != 0 && (Timestamp::now() - Timestamp{std::chrono::nanoseconds{lastDrop}}) <
                             std::chrono::nanoseconds{kDegradedAfterDrop};
    return Health{.state = recentDrop ? HealthState::Degraded : HealthState::Ok,
                  .detail = std::move(detail)};
}

void TrackingModule::publish(std::vector<ObjectRecord>& records) {
    for (const auto& r : records) {
        bus_->publish(r);
    }
    published_.fetch_add(records.size(), std::memory_order_relaxed);
    records.clear();
}

void TrackingModule::logCounters() {
    auto counters = tracker_->counters();
    if (counters.empty() || counters == lastLogged_) {
        return;
    }
    std::string line;
    for (const auto& c : counters) {
        constexpr std::array<const char*, 3> kPhase{"silent", "starting", "running"};
        line += " | L" + std::to_string(c.laneId) + "/S" + std::to_string(c.sensorId) + " cam" +
                std::to_string(c.cameraId) + " " + kPhase.at(static_cast<std::size_t>(c.phase)) +
                ": " + std::to_string(c.count) + " cups, " + std::to_string(c.noData) +
                " no data, " + std::to_string(c.timingMisses) + " missed, " +
                std::to_string(c.repairs) + " repairs, " + std::to_string(c.extraFrames) +
                " extra, " + std::to_string(c.idResets) + " id resets";
    }
    spdlog::info("tracking{} | inbox dropped {}", line, inbox_.dropped());
    lastLogged_ = std::move(counters);
}

void TrackingModule::run(const std::stop_token& stop) {
    std::vector<ObjectRecord> records;
    auto nextTick = Timestamp::now();
    auto nextLog = nextTick + options_.logInterval;
    while (!stop.stop_requested()) {
        std::size_t handled = 0;
        while (handled < kMaxFramesPerLoop) {
            const auto meta = inbox_.pop();
            if (!meta) {
                break;
            }
            tracker_->onFrame(*meta, records);
            publish(records);
            ++handled;
        }
        processed_.fetch_add(handled, std::memory_order_relaxed);

        const auto now = Timestamp::now();
        if (const auto dropped = inbox_.dropped(); dropped != droppedSeen_) {
            droppedSeen_ = dropped;
            lastDropNs_.store(now.ns(), std::memory_order_relaxed);
        }
        if (now >= nextTick) {
            tracker_->onTick(now, records);
            publish(records);
            nextTick = now + options_.tickInterval;
        }
        if (now >= nextLog) {
            logCounters();
            saveState(); // learned phases survive a crash too
            nextLog = now + options_.logInterval;
        }
        if (handled == 0) {
            std::this_thread::sleep_for(1ms);
        }
    }
}

} // namespace vsort::service

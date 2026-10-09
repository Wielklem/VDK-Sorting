#include "analysis/analysis_module.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <system_error>
#include <utility>

#include <opencv2/imgcodecs.hpp>
#include <spdlog/spdlog.h>

#include <vsort/common/machine_config.hpp>
#include <vsort/common/roi_config.hpp>
#include <vsort/common/timestamp.hpp>

#include "analysis/analysis_overlay.hpp"
#include "analysis/overlay.hpp"
#include "tracking/lane_crop.hpp"

namespace vsort::service {

using namespace std::chrono_literals;

namespace {
constexpr auto kDegradedAfterDrop = 10s;
constexpr auto kExpireInterval = 100ms;
} // namespace

AnalysisModule::AnalysisModule(const IConfigStore* config, AnalysisOptions options)
    : config_{config}
    , options_{std::move(options)}
    , results_{options_.resultQueue} {}

AnalysisModule::~AnalysisModule() {
    stop();
}

Result<> AnalysisModule::init(ModuleContext& context) {
    bus_ = &context.bus;
    if (config_ == nullptr) {
        return makeError(Errc::InvalidArgument, "analysis: no config store");
    }
    auto machine = loadMachineConfig(*config_);
    if (!machine) {
        return std::unexpected{std::move(machine.error())};
    }
    auto analysisConfig = loadAnalysisConfig(*config_);
    if (!analysisConfig) {
        return std::unexpected{std::move(analysisConfig.error())};
    }
    analysisConfig_ = std::move(*analysisConfig);
    const auto rois = config_->get(kRoiModule);
    const auto sensors = makeTrackedSensors(*machine, rois ? *rois : nlohmann::json::object());

    {
        const std::scoped_lock lock{configMutex_};
        current_ = std::make_shared<const AnalysisConfig>(analysisConfig_);
    }
    workers_.clear();
    for (const auto& s : sensors) {
        auto& worker = workers_[s.cameraId];
        if (!worker) {
            worker = std::make_unique<Worker>(options_.frameQueue);
            worker->cameraId = s.cameraId;
            worker->config = current_;
            worker->generation = generation_.load(std::memory_order_acquire);
        }
        const auto& params = analysisConfig_.forSensor(s.sensorId);
        SensorRuntime runtime;
        runtime.sensor = s;
        runtime.params = params;
        runtime.pipeline = makeCupPipeline(params);
        worker->sensors.push_back(std::move(runtime));
    }
    if (analysisConfig_.enabled) {
        records_ = bus_->subscribe<ObjectRecord>(options_.recordQueue);
        configChanges_ = bus_->subscribe<ConfigChanged>(64);
    }
    std::string stages;
    for (const auto name : makeCupPipeline(analysisConfig_.defaults).stageNames()) {
        stages += (stages.empty() ? "" : " > ") + std::string{name};
    }
    spdlog::info("analysis: {} sensor(s) on {} camera(s), stages {}{}{}", sensors.size(),
                 workers_.size(), stages, analysisConfig_.enabled ? "" : " (disabled)",
                 analysisConfig_.debugEveryN > 0 && !options_.debugDir.empty()
                     ? std::format(", debug image every {} frames in {}",
                                   analysisConfig_.debugEveryN, options_.debugDir.generic_string())
                     : std::string{});
    return {};
}

Result<> AnalysisModule::start() {
    if (bus_ == nullptr) {
        return makeError(Errc::InvalidArgument, "analysis: start() before init()");
    }
    if (!analysisConfig_.enabled) {
        return {};
    }
    for (auto& [id, worker] : workers_) {
        Worker* w = worker.get();
        w->thread = std::jthread{[this, w](const std::stop_token& stop) { runWorker(*w, stop); }};
    }
    joinThread_ = std::jthread{[this](const std::stop_token& stop) { runJoin(stop); }};
    running_.store(true, std::memory_order_release);
    return {};
}

void AnalysisModule::stop() noexcept {
    running_.store(false, std::memory_order_release);
    for (auto& [id, worker] : workers_) {
        if (worker->thread.joinable()) {
            worker->thread.request_stop();
            worker->thread.join();
        }
        while (worker->queue.tryPop()) { // releases the camera buffers
        }
    }
    if (joinThread_.joinable()) {
        joinThread_.request_stop();
        joinThread_.join();
    }
}

Health AnalysisModule::health() const {
    std::string detail =
        std::to_string(framesAnalysed()) + " frames analysed, " + std::to_string(framesDropped()) +
        " dropped (queue full), " + std::to_string(measurementsPublished()) + " measurements, " +
        std::to_string(expired_.load(std::memory_order_relaxed)) + " cups without result";
    const auto lastDrop = lastDropNs_.load(std::memory_order_relaxed);
    const bool recentDrop =
        lastDrop != 0 && (Timestamp::now() - Timestamp{std::chrono::nanoseconds{lastDrop}}) <
                             std::chrono::nanoseconds{kDegradedAfterDrop};
    return Health{.state = recentDrop ? HealthState::Degraded : HealthState::Ok,
                  .detail = std::move(detail)};
}

void AnalysisModule::submit(const camera::Frame& frame) noexcept {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    const auto it = workers_.find(frame.meta.cameraIndex);
    if (it == workers_.end()) {
        return; // no sensor uses this camera
    }
    it->second->received.fetch_add(1, std::memory_order_relaxed);
    camera::Frame kept = frame;
    if (!kept.owner) { // pixels only valid during the callback: keep a copy
        try {
            auto copy =
                std::make_shared<std::vector<std::byte>>(frame.data.begin(), frame.data.end());
            kept.data = *copy;
            kept.owner = std::move(copy);
        } catch (...) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    if (!it->second->queue.tryPush(std::move(kept))) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        lastDropNs_.store(Timestamp::now().ns(), std::memory_order_relaxed);
    }
}

void AnalysisModule::refresh(Worker& worker) {
    if (generation_.load(std::memory_order_acquire) == worker.generation) {
        return;
    }
    {
        const std::scoped_lock lock{configMutex_};
        worker.config = current_;
        worker.generation = generation_.load(std::memory_order_acquire);
    }
    for (auto& s : worker.sensors) {
        const auto& params = worker.config->forSensor(s.sensor.sensorId);
        if (params != s.params) {
            s.params = params;
            s.pipeline = makeCupPipeline(params);
        }
    }
}

void AnalysisModule::reloadConfig() {
    auto loaded = loadAnalysisConfig(*config_);
    if (!loaded) {
        spdlog::warn("analysis: new config not applied: {}", loaded.error().what());
        return;
    }
    if (loaded->enabled != analysisConfig_.enabled) {
        spdlog::warn("analysis: 'enabled' changes need a service restart");
    }
    {
        const std::scoped_lock lock{configMutex_};
        current_ = std::make_shared<const AnalysisConfig>(std::move(*loaded));
        generation_.fetch_add(1, std::memory_order_acq_rel);
    }
    const auto version = config_->version(kAnalysisModule);
    spdlog::info("analysis: config version {} applied", version ? version->number : 0U);
}

void AnalysisModule::runWorker(Worker& worker, const std::stop_token& stop) {
    auto nextLog = Timestamp::now() + options_.logInterval;
    while (!stop.stop_requested()) {
        auto frame = worker.queue.tryPop();
        if (frame) {
            refresh(worker); // a config saved before this frame was taken applies to it
            analyse(worker, *frame);
        } else {
            std::this_thread::sleep_for(1ms);
        }
        if (const auto now = Timestamp::now(); now >= nextLog) {
            for (auto& sensor : worker.sensors) {
                logWindow(worker.cameraId, sensor);
            }
            nextLog = now + options_.logInterval;
        }
    }
}

void AnalysisModule::analyse(Worker& worker, const camera::Frame& frame) {
    const auto& meta = frame.meta;
    const auto alignment = cropAlignment(meta.pixelFormat);
    std::vector<StageTiming> timings;
    bool allOk = true;
    for (auto& s : worker.sensors) {
        ++s.frames;
        auto& w = s.window;
        ++w.frames;
        const auto start = std::chrono::steady_clock::now();
        const auto lane = toPixels(s.sensor.roi, meta.width, meta.height, alignment);
        const auto region =
            analysisRegion(lane, s.params.roiBufferPx, meta.width, meta.height, alignment);
        AnalysisContext ctx;
        auto ok = prepareContext(frame, region, ctx);
        if (ok) {
            ok = s.pipeline.run(ctx, timings);
        }
        const std::chrono::duration<double, std::milli> took =
            std::chrono::steady_clock::now() - start;
        w.msSum += took.count();
        w.msMax = std::max(w.msMax, took.count());
        for (const auto& t : timings) {
            w.stageMs[t.stage] += t.ms;
        }
        if (!ok) {
            ++w.failed;
            w.lastError = ok.error().what();
            allOk = false;
            continue;
        }
        const auto& cfg = *worker.config;
        if (cfg.debugEveryN > 0 && !options_.debugDir.empty() && s.frames % cfg.debugEveryN == 0) {
            saveDebug(s, frame, ctx, cfg.debugMaxImages);
        }
        if (options_.publishOverlays) {
            bus_->publish(makeOverlay(ctx, region, lane, meta, s.sensor.sensorId));
        }
        if (!results_.tryPush(SensorResult{.sensorId = s.sensor.sensorId,
                                           .frameId = meta.frameId,
                                           .values = std::move(ctx.values)})) {
            resultsDropped_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    analysed_.fetch_add(1, std::memory_order_relaxed);
    if (allOk) {
        worker.analysedOk.fetch_add(1, std::memory_order_relaxed);
    }
}

// <debugDir>/sensor_<id>/<wall clock ns>_f<frame id>.jpg; names sort by time.
void AnalysisModule::saveDebug(SensorRuntime& sensor, const camera::Frame& frame,
                               const AnalysisContext& ctx, std::uint32_t maxImages) {
    namespace fs = std::filesystem;
    const fs::path dir = options_.debugDir / ("sensor_" + std::to_string(sensor.sensor.sensorId));
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        sensor.window.lastError = "debug image: " + ec.message();
        return;
    }
    if (!sensor.debugScanned) {
        sensor.debugScanned = true;
        std::vector<fs::path> existing;
        for (const auto& entry : fs::directory_iterator{dir, ec}) {
            if (entry.path().extension() == ".jpg") {
                existing.push_back(entry.path());
            }
        }
        std::ranges::sort(existing);
        sensor.debugFiles.assign(existing.begin(), existing.end());
    }
    const cv::Mat image = drawOverlay(ctx);
    if (image.empty()) {
        return;
    }
    const fs::path file =
        dir / std::format("{:019}_f{}.jpg", WallTime::now().ns(), frame.meta.frameId.value());
    try {
        if (!cv::imwrite(file.string(), image)) {
            sensor.window.lastError = "debug image: cannot write " + file.generic_string();
            return;
        }
    } catch (const cv::Exception& e) {
        sensor.window.lastError = std::string{"debug image: "} + e.what();
        return;
    }
    sensor.debugFiles.push_back(file);
    while (sensor.debugFiles.size() > maxImages) {
        fs::remove(sensor.debugFiles.front(), ec);
        sensor.debugFiles.pop_front();
    }
}

void AnalysisModule::logWindow(std::uint16_t cameraId, SensorRuntime& sensor) {
    auto& w = sensor.window;
    if (w.frames == 0) {
        return;
    }
    const auto n = static_cast<double>(w.frames);
    std::string stages;
    for (const auto& [stage, ms] : w.stageMs) {
        stages += std::format("{}{} {:.1f}", stages.empty() ? "" : ", ", stage, ms / n);
    }
    const std::string line =
        std::format("analysis S{} cam{}: {} frames, {:.1f} ms mean, {:.1f} max ({})",
                    sensor.sensor.sensorId, cameraId, w.frames, w.msSum / n, w.msMax, stages);
    if (w.failed > 0) {
        spdlog::warn("{} | {} failed, last: {}", line, w.failed, w.lastError);
    } else {
        spdlog::info("{}", line);
    }
    w = Window{};
}

void AnalysisModule::runJoin(const std::stop_token& stop) {
    ResultJoin join{options_.join};
    std::vector<Measurement> out;
    auto nextExpire = Timestamp::now();
    auto nextLog = nextExpire + options_.logInterval;
    std::uint64_t loggedJoined = 0;
    std::uint64_t loggedExpired = 0;
    std::map<std::uint16_t, RateCounts> rateCounts;
    auto lastRates = nextExpire;
    while (!stop.stop_requested()) {
        std::size_t handled = 0;
        while (auto result = results_.tryPop()) {
            join.addResult(std::move(*result), out);
            ++handled;
        }
        handled += records_->drain(
            [&](const ObjectRecord& record) { join.addRecord(record, Timestamp::now(), out); });
        bool reload = false;
        configChanges_->drain(
            [&](const ConfigChanged& c) { reload = reload || c.module == kAnalysisModule; });
        if (reload) {
            reloadConfig();
        }
        for (const auto& m : out) {
            bus_->publish(m);
        }
        published_.fetch_add(out.size(), std::memory_order_relaxed);
        out.clear();

        const auto now = Timestamp::now();
        if (now >= nextExpire) {
            join.expire(now);
            expired_.store(join.expired(), std::memory_order_relaxed);
            nextExpire = now + kExpireInterval;
        }
        if (now >= nextLog) {
            if (join.joined() != loggedJoined || join.expired() != loggedExpired) {
                spdlog::info("analysis: {} measurements, {} cups without result, {} results "
                             "dropped, {} frames dropped",
                             join.joined(), join.expired(),
                             resultsDropped_.load(std::memory_order_relaxed), framesDropped());
                loggedJoined = join.joined();
                loggedExpired = join.expired();
            }
            nextLog = now + options_.logInterval;
        }
        if (now - lastRates >= options_.ratesInterval) {
            publishRates(now - lastRates, rateCounts);
            lastRates = now;
        }
        if (handled == 0) {
            std::this_thread::sleep_for(1ms);
        }
    }
}

// Rates over the time since the last call; `last` keeps the counters of that call.
void AnalysisModule::publishRates(Timestamp::duration elapsed,
                                  std::map<std::uint16_t, RateCounts>& last) {
    const double seconds = std::chrono::duration<double>(elapsed).count();
    if (seconds <= 0.0) {
        return;
    }
    CameraRates rates;
    rates.cameras.reserve(workers_.size());
    for (const auto& [id, worker] : workers_) {
        const RateCounts now{.received = worker->received.load(std::memory_order_relaxed),
                             .analysedOk = worker->analysedOk.load(std::memory_order_relaxed)};
        RateCounts& before = last[id];
        rates.cameras.push_back(CameraRate{
            .cameraId = id,
            .incomingFps = static_cast<double>(now.received - before.received) / seconds,
            .analysedFps = static_cast<double>(now.analysedOk - before.analysedOk) / seconds});
        before = now;
    }
    bus_->publish(rates);
}

} // namespace vsort::service

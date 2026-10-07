#include "camera/camera_manager.hpp"

#include <algorithm>
#include <format>
#include <utility>

#include <spdlog/spdlog.h>

#include <vsort/camera/camera_settings_config.hpp>

namespace vsort::service {
namespace {

CameraRuntimeState toRuntime(camera::CameraState state) noexcept {
    switch (state) {
    case camera::CameraState::Open:
        return CameraRuntimeState::Open;
    case camera::CameraState::Streaming:
        return CameraRuntimeState::Streaming;
    case camera::CameraState::Reconnecting:
        return CameraRuntimeState::Reconnecting;
    case camera::CameraState::Closed:
        break;
    }
    return CameraRuntimeState::Closed;
}

} // namespace

CameraManager::CameraManager(std::unique_ptr<ICameraBackend> backend, IConfigStore* config,
                             CameraManagerOptions options)
    : backend_{std::move(backend)}
    , config_{config}
    , options_{options} {}

CameraManager::~CameraManager() {
    stop();
}

void CameraManager::addFrameSink(FrameSink sink) {
    sinks_.push_back(std::move(sink));
}

void CameraManager::setOnListChanged(std::function<void()> callback) {
    onListChanged_ = std::move(callback);
}

Result<> CameraManager::start() {
    if (running_) {
        return makeError(Errc::AlreadyExists, "camera manager already started");
    }
    if (!backend_) {
        return makeError(Errc::InvalidArgument, "camera manager has no backend");
    }
    persistent_ = backend_->persistent();
    std::vector<camera::CameraMapEntry> entries;
    if (persistent_) {
        if (config_ == nullptr) {
            return makeError(Errc::InvalidArgument, "camera manager needs a config store");
        }
        const auto map = camera::loadCameraMap(*config_);
        if (!map) {
            return std::unexpected{map.error()};
        }
        if (const auto settings = camera::loadCameraSettings(*config_); !settings) {
            return std::unexpected{settings.error()};
        }
        entries = map->entries();
    } else {
        entries = backend_->fixedMap();
    }
    {
        const std::scoped_lock lock{mutex_};
        slots_.clear();
        for (const auto& entry : entries) {
            addSlotLocked(entry);
        }
    }
    failed_ = false;
    running_ = true;
    thread_ = std::jthread{[this](const std::stop_token& token) { run(token); }};
    spdlog::info("cameras: backend '{}', {} mapped camera(s){}", backend_->name(), entries.size(),
                 options_.forceFreeRun ? ", free-run forced" : "");
    return {};
}

void CameraManager::stop() noexcept {
    try {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
        std::vector<std::shared_ptr<camera::IMonitoredCamera>> cameras;
        {
            const std::scoped_lock lock{mutex_};
            for (auto& entry : slots_) {
                if (entry.second->camera) {
                    cameras.push_back(std::move(entry.second->camera));
                }
            }
        }
        for (auto& cam : cameras) {
            cam->stop(); // no more frame callbacks after this
        }
        for (auto& cam : cameras) {
            cam->close();
        }
        running_ = false;
    } catch (...) { // NOLINT(bugprone-empty-catch): stop() must not throw
    }
}

void CameraManager::addSlotLocked(const camera::CameraMapEntry& entry) {
    auto slot = std::make_shared<Slot>();
    slot->id = entry.id;
    slot->serial = entry.serial;
    slots_[entry.id] = std::move(slot);
}

std::vector<CameraListEntry> CameraManager::list() const {
    struct Row {
        CameraListEntry entry;
        std::shared_ptr<camera::IMonitoredCamera> camera;
    };
    std::vector<Row> rows;
    {
        const std::scoped_lock lock{mutex_};
        for (const auto& [id, slot] : slots_) {
            Row row;
            row.entry.id = id;
            row.entry.serial = slot->serial;
            row.entry.model = slot->model;
            row.camera = slot->camera;
            rows.push_back(std::move(row));
        }
    }
    std::vector<CameraListEntry> out;
    for (auto& row : rows) {
        row.entry.state =
            row.camera ? toRuntime(row.camera->health().state) : CameraRuntimeState::Closed;
        out.push_back(std::move(row.entry));
    }
    return out;
}

Result<std::shared_ptr<camera::IMonitoredCamera>>
CameraManager::connectedCamera(std::uint16_t id) const {
    const std::scoped_lock lock{mutex_};
    const auto it = slots_.find(id);
    if (it == slots_.end()) {
        return makeError(Errc::NotFound, "unknown camera id " + std::to_string(id));
    }
    if (!it->second->camera) {
        return makeError(Errc::DeviceError, "camera " + std::to_string(id) + " is not connected");
    }
    return it->second->camera;
}

Result<camera::CameraSettings> CameraManager::settings(std::uint16_t id) const {
    const auto cam = connectedCamera(id);
    if (!cam) {
        return std::unexpected{cam.error()};
    }
    return (*cam)->settings();
}

Result<> CameraManager::apply(std::uint16_t id, const camera::CameraSettings& settings) {
    const auto cam = connectedCamera(id);
    if (!cam) {
        return std::unexpected{cam.error()};
    }
    camera::CameraSettings applied = settings;
    if (options_.forceFreeRun) {
        applied.triggerMode = camera::TriggerMode::FreeRun;
    }
    if (const auto result = (*cam)->applySettings(applied); !result) {
        return result;
    }
    persist(id, settings);
    const std::scoped_lock lock{mutex_};
    if (const auto it = slots_.find(id); it != slots_.end()) {
        it->second->settings = applied;
    }
    return {};
}

// The camera already took the values; a failed save is only logged.
void CameraManager::persist(std::uint16_t id, camera::CameraSettings settings) {
    if (!persistent_ || config_ == nullptr) {
        return;
    }
    const std::scoped_lock lock{configMutex_};
    if (options_.forceFreeRun) {
        // Free run is a bench override: keep the saved trigger setting.
        camera::CameraSettings stored;
        if (const auto all = camera::loadCameraSettings(*config_); all) {
            if (const auto it = all->find(id); it != all->end()) {
                stored = it->second;
            }
        }
        settings.triggerMode = stored.triggerMode;
        settings.triggerEdge = stored.triggerEdge;
    }
    if (const auto saved = camera::saveCameraSettings(*config_, id, settings, "hmi"); !saved) {
        spdlog::warn("camera {}: settings applied but not saved: {}", id, saved.error().what());
    }
}

Health CameraManager::health() const {
    if (failed_) {
        return Health{HealthState::Failed, "camera supervisor stopped unexpectedly"};
    }
    if (!running_) {
        return Health{HealthState::Unknown, {}};
    }
    const auto cameras = list();
    if (cameras.empty()) {
        return Health{HealthState::Degraded, "no cameras mapped or found"};
    }
    for (const auto& cam : cameras) {
        if (cam.state != CameraRuntimeState::Streaming) {
            return Health{HealthState::Degraded,
                          std::format("camera {} ({}) is not streaming", cam.id, cam.serial)};
        }
    }
    return Health{HealthState::Ok, {}};
}

void CameraManager::run(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        try {
            scanOnce();
            publishStateChanges();
        } catch (const std::exception& ex) {
            spdlog::error("cameras: supervisor stopped: {}", ex.what());
            failed_ = true;
            return;
        }
        std::unique_lock lock{wakeMutex_};
        wake_.wait_for(lock, stop, options_.scanInterval, [] { return false; });
    }
}

bool CameraManager::needsDiscovery() const {
    const std::scoped_lock lock{mutex_};
    if (slots_.empty()) {
        return persistent_; // auto-assign; a fixed map without cameras has nothing to find
    }
    return std::ranges::any_of(slots_, [](const auto& entry) { return !entry.second->camera; });
}

void CameraManager::scanOnce() {
    if (!needsDiscovery()) {
        return;
    }
    const auto found = backend_->discover();
    if (!found) {
        const std::string text = found.error().what();
        if (text != lastDiscoverError_) {
            spdlog::warn("cameras: discovery failed: {}", text);
            lastDiscoverError_ = text;
        }
        return;
    }
    lastDiscoverError_.clear();

    bool mapEmpty = false;
    {
        const std::scoped_lock lock{mutex_};
        mapEmpty = slots_.empty();
    }
    if (persistent_ && mapEmpty) {
        assignAutomatically(*found);
    }

    std::vector<std::shared_ptr<Slot>> pending;
    {
        const std::scoped_lock lock{mutex_};
        for (const auto& entry : slots_) {
            if (!entry.second->camera) {
                pending.push_back(entry.second);
            }
        }
    }
    for (const auto& slot : pending) {
        const auto it = std::ranges::find(*found, slot->serial, &camera::DiscoveredCamera::serial);
        if (it == found->end()) {
            warnOnce("missing:" + slot->serial,
                     std::format("camera {} ({}) not found", slot->id, slot->serial));
            continue;
        }
        connect(slot, *it);
    }

    for (const auto& cam : *found) {
        bool known = false;
        {
            const std::scoped_lock lock{mutex_};
            known = std::ranges::any_of(
                slots_, [&](const auto& entry) { return entry.second->serial == cam.serial; });
        }
        if (!known) {
            warnOnce("unmapped:" + cam.serial,
                     std::format("camera {} is not in the camera map and is ignored", cam.serial));
        }
    }
}

// Empty camera map (fresh install): number the cameras found by serial number and save the map.
void CameraManager::assignAutomatically(const std::vector<camera::DiscoveredCamera>& found) {
    if (found.empty()) {
        return;
    }
    std::vector<std::string> serials;
    for (const auto& cam : found) {
        serials.push_back(cam.serial);
    }
    std::ranges::sort(serials);
    std::vector<camera::CameraMapEntry> entries;
    for (std::size_t i = 0; i < serials.size(); ++i) {
        entries.push_back({.serial = serials[i], .id = static_cast<std::uint16_t>(i)});
    }
    const auto map = camera::CameraMap::create(std::move(entries));
    if (!map) {
        spdlog::warn("cameras: cannot build a camera map: {}", map.error().what());
        return;
    }
    {
        const std::scoped_lock lock{configMutex_};
        if (const auto saved = config_->set(camera::kCameraMapModule, map->toJson(), "camera");
            !saved) {
            spdlog::error("cameras: cannot save the camera map: {}", saved.error().what());
            return;
        }
    }
    const std::scoped_lock lock{mutex_};
    for (const auto& entry : map->entries()) {
        addSlotLocked(entry);
    }
    spdlog::info("cameras: camera map was empty, assigned {} camera(s) by serial number",
                 map->entries().size());
}

camera::CameraSettings CameraManager::wantedSettings(std::uint16_t id) {
    camera::CameraSettings wanted;
    if (persistent_ && config_ != nullptr) {
        const std::scoped_lock lock{configMutex_};
        if (const auto all = camera::loadCameraSettings(*config_); all) {
            if (const auto it = all->find(id); it != all->end()) {
                wanted = it->second;
            }
        } else {
            spdlog::warn("camera {}: cannot read saved settings: {}", id, all.error().what());
        }
    }
    if (options_.forceFreeRun) {
        wanted.triggerMode = camera::TriggerMode::FreeRun;
    }
    return wanted;
}

camera::FrameCallback CameraManager::callbackFor(std::uint16_t id) {
    return [this, id](const camera::Frame& frame) {
        camera::Frame tagged = frame;
        tagged.meta.cameraIndex = id;
        for (const auto& sink : sinks_) {
            sink(tagged);
        }
    };
}

void CameraManager::connect(const std::shared_ptr<Slot>& slot,
                            const camera::DiscoveredCamera& found) {
    const camera::CameraSettings wanted = wantedSettings(slot->id);
    std::shared_ptr<camera::IMonitoredCamera> cam =
        camera::makeResilientCamera([this] { return backend_->create(); }, options_.resilient);

    if (const auto opened = cam->open(slot->serial); !opened) {
        warnOnce("open:" + slot->serial + opened.error().message,
                 std::format("camera {} ({}): open failed: {}", slot->id, slot->serial,
                             opened.error().what()));
        return;
    }
    if (const auto applied = cam->applySettings(wanted); !applied) {
        spdlog::warn("camera {} ({}): saved settings rejected, keeping the camera's own: {}",
                     slot->id, slot->serial, applied.error().what());
    }
    cam->setFrameCallback(callbackFor(slot->id));
    if (const auto started = cam->start(); !started) {
        cam->close();
        warnOnce("start:" + slot->serial + started.error().message,
                 std::format("camera {} ({}): start failed: {}", slot->id, slot->serial,
                             started.error().what()));
        return;
    }

    camera::CameraSettings active = wanted;
    if (const auto current = cam->settings(); current) {
        active = *current;
    }
    std::string model = found.model;
    if (const auto info = cam->info(); info && !info->model.empty()) {
        model = info->model;
    }
    {
        const std::scoped_lock lock{mutex_};
        slot->camera = std::move(cam);
        slot->settings = active;
        slot->model = std::move(model);
    }
    spdlog::info("camera {} ({}) streaming", slot->id, slot->serial);
}

void CameraManager::publishStateChanges() {
    std::string signature;
    for (const auto& cam : list()) {
        signature +=
            std::format("{}:{}:{}:{};", cam.id, cam.serial, cam.model, static_cast<int>(cam.state));
    }
    if (signature == lastSignature_) {
        return;
    }
    lastSignature_ = std::move(signature);
    if (onListChanged_) {
        onListChanged_();
    }
}

void CameraManager::warnOnce(const std::string& key, const std::string& message) {
    if (warned_.insert(key).second) {
        spdlog::warn("{}", message);
    } else {
        spdlog::debug("{}", message);
    }
}

} // namespace vsort::service

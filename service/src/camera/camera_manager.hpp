#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/camera/camera_map.hpp>
#include <vsort/camera/resilient_camera.hpp>
#include <vsort/common/config_store.hpp>
#include <vsort/common/error.hpp>
#include <vsort/common/module.hpp>

#include "camera/camera_backend.hpp"
#include "ipc/camera_access.hpp"

namespace vsort::service {

// Receives every frame of every camera, on the camera's grab thread. Must be fast, must not throw.
using FrameSink = std::function<void(const camera::Frame&)>;

struct CameraManagerOptions {
    std::chrono::milliseconds scanInterval{2000}; // discovery / state check period
    bool forceFreeRun{false};                     // bench: free-run at open, never saved
    camera::ResilientOptions resilient;
};

// Owns the cameras of the service (docs/vision-platform-architecture.md, section 40.20).
// A supervisor thread discovers cameras and opens, configures and starts every mapped camera that
// is not connected, so a camera that is missing at startup or plugged in later is picked up.
// All public methods are thread-safe, except addFrameSink() and setOnListChanged(): call those
// before start().
//
// The config modules camera_map and camera_settings must be registered by the owner (persistent
// backends only).
class CameraManager {
public:
    // `config` may be null only for a non-persistent backend; it must outlive the manager.
    CameraManager(std::unique_ptr<ICameraBackend> backend, IConfigStore* config,
                  CameraManagerOptions options = {});
    ~CameraManager();
    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;
    CameraManager(CameraManager&&) = delete;
    CameraManager& operator=(CameraManager&&) = delete;

    void addFrameSink(FrameSink sink);
    // Called from the supervisor thread when the camera list or a camera state changed.
    void setOnListChanged(std::function<void()> callback);

    [[nodiscard]] Result<> start();
    void stop() noexcept; // stops the cameras; idempotent

    // ICameraAccess semantics (see ipc/camera_access.hpp).
    [[nodiscard]] std::vector<CameraListEntry> list() const;
    [[nodiscard]] Result<camera::CameraSettings> settings(std::uint16_t id) const;
    // Applies to the camera and, when persistent, saves it. DeviceError while not connected.
    [[nodiscard]] Result<> apply(std::uint16_t id, const camera::CameraSettings& settings);

    [[nodiscard]] Health health() const;

private:
    struct Slot {
        std::uint16_t id{0};
        std::string serial;
        std::string model;
        std::shared_ptr<camera::IMonitoredCamera> camera; // null until connected
        camera::CameraSettings settings;
    };

    void run(const std::stop_token& stop);
    void scanOnce();
    [[nodiscard]] bool needsDiscovery() const;
    void assignAutomatically(const std::vector<camera::DiscoveredCamera>& found);
    void connect(const std::shared_ptr<Slot>& slot, const camera::DiscoveredCamera& found);
    void publishStateChanges();
    void addSlotLocked(const camera::CameraMapEntry& entry);
    void persist(std::uint16_t id, camera::CameraSettings settings);
    void warnOnce(const std::string& key, const std::string& message);
    [[nodiscard]] camera::CameraSettings wantedSettings(std::uint16_t id);
    [[nodiscard]] camera::FrameCallback callbackFor(std::uint16_t id);
    [[nodiscard]] Result<std::shared_ptr<camera::IMonitoredCamera>>
    connectedCamera(std::uint16_t id) const;

    std::unique_ptr<ICameraBackend> backend_;
    IConfigStore* config_;
    CameraManagerOptions options_;
    std::vector<FrameSink> sinks_;
    std::function<void()> onListChanged_;
    bool persistent_{true};

    mutable std::mutex mutex_; // slots_ and the Slot fields
    std::map<std::uint16_t, std::shared_ptr<Slot>> slots_;
    std::mutex configMutex_; // read-modify-write of the config modules

    // Supervisor thread only:
    std::set<std::string> warned_;
    std::string lastSignature_;
    std::string lastDiscoverError_;

    std::mutex wakeMutex_;
    std::condition_variable_any wake_;
    std::atomic<bool> running_{false};
    std::atomic<bool> failed_{false};
    std::jthread thread_; // last: destroyed (and joined) first
};

} // namespace vsort::service

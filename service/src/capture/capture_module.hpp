#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <vsort/common/bounded_queue.hpp>
#include <vsort/common/machine_config.hpp>
#include <vsort/common/module.hpp>

#include "capture/capture_control.hpp"
#include "capture/capture_sink.hpp"

namespace vsort::service {

struct CaptureOptions {
    std::filesystem::path defaultFolder; // what the HMI proposes until the user picks a folder
    std::size_t queueDepth{32};          // photos waiting for the writer thread
    std::uint64_t minFreeBytes{500ULL * 1024 * 1024}; // a capture does not start below this
    int jpegQuality{95};
};

// All sensors of the machine config, in config order.
[[nodiscard]] std::vector<CaptureSensor> captureSensors(const MachineConfig& machine);

// IModule "capture" (P60.100): saves photos of lane ROIs for performance review.
//
// Per selected sensor it saves `photosPerSensor` photos that pass the filter, then the capture is
// Done. Per photo it writes <session>/sensor_<id>/<seq>_<verdict>_n<count>[_<len>mm]_f<frame>.jpg
// (the lane ROI with detection buffer, as the analysis saw it) and the same name with _overlay
// (mask, objects and sizes drawn), and one line in <session>/captures.csv. <session> is a new
// folder <folder>/<YYYYMMDDTHHMMSS>. The analysis offers the photos through ICaptureSink; the
// disk writes run on a writer thread, so the analysis never waits for the disk.
class CaptureModule final : public IModule, public ICaptureControl, public ICaptureSink {
public:
    CaptureModule(std::vector<CaptureSensor> sensors, CaptureOptions options);
    ~CaptureModule() override;

    [[nodiscard]] std::string_view name() const noexcept override { return "capture"; }
    [[nodiscard]] std::vector<std::string> dependencies() const override { return {"analysis"}; }
    [[nodiscard]] Result<> init(ModuleContext& context) override;
    [[nodiscard]] Result<> start() override;
    void stop() noexcept override;
    [[nodiscard]] Health health() const override;

    [[nodiscard]] Result<CaptureStatus> startCapture(const CaptureRequest& request) override;
    [[nodiscard]] CaptureStatus stopCapture() override;
    [[nodiscard]] CaptureStatus captureStatus() const override;

    [[nodiscard]] std::optional<std::uint32_t>
    reserve(std::uint16_t sensorId, const CaptureValues& values) noexcept override;
    void commit(CaptureImage image) noexcept override;
    void cancel(std::uint16_t sensorId) noexcept override;

private:
    struct SensorState {
        CaptureSensor sensor;
        bool selected{false};
        std::uint32_t reserved{0}; // photos handed out by reserve(), minus cancelled ones
        std::uint32_t saved{0};    // photos on disk
    };

    [[nodiscard]] CaptureStatus statusLocked() const;
    [[nodiscard]] SensorState* findLocked(std::uint16_t sensorId);
    void stopWriter() noexcept;
    void runWriter(const std::stop_token& stop);
    void write(const CaptureImage& image);
    void closeCsvWhenFinished();
    void fail(const std::string& what);

    CaptureOptions options_;
    std::mutex control_; // serialises start / stop (never held by the writer thread)

    mutable std::mutex mutex_; // state below, also taken by the analysis threads and the writer
    CaptureState state_{CaptureState::Idle};
    CaptureFilter filter_;
    std::uint32_t target_{0};
    std::vector<SensorState> sensors_;
    std::filesystem::path session_;
    std::string errorText_;

    std::atomic<bool> active_{false}; // fast path of reserve(): true while Running
    BoundedQueue<CaptureImage> queue_;
    std::ofstream csv_; // writer thread only
    std::jthread writer_;
};

} // namespace vsort::service

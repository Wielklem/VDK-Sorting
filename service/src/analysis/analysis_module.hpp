#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/common/bounded_queue.hpp>
#include <vsort/common/config_store.hpp>
#include <vsort/common/message_bus.hpp>
#include <vsort/common/module.hpp>
#include <vsort/common/timestamp.hpp>

#include "analysis/analysis_config.hpp"
#include "analysis/camera_rates.hpp"
#include "analysis/pipeline.hpp"
#include "analysis/result_join.hpp"
#include "tracking/frame_sequence_tracker.hpp"

namespace vsort::service {

struct AnalysisOptions {
    std::size_t frameQueue{4};            // per camera; full = frame not analysed (counted)
    std::size_t resultQueue{256};         // camera threads -> join thread
    std::size_t recordQueue{4096};        // ObjectRecords waiting on the bus
    std::chrono::seconds logInterval{10}; // run time per sensor in the log
    JoinOptions join;
    std::filesystem::path debugDir; // annotated images (debug_every_n); empty = never
    std::chrono::milliseconds ratesInterval{1000}; // CameraRates on the bus (P30.86)
};

// IModule "analysis" (M60, P60.10). Frames come in through submit() (a camera frame sink) and are
// analysed right away on one thread per camera, for every sensor (lane ROI) of that camera; the
// frame buffer is released when the pipeline is done. A join thread pairs the results with the
// ObjectRecords on the bus and publishes Measurements (MSG-60-01) and, every ratesInterval, the
// incoming and analysed frame rate per camera (CameraRates, P30.86).
// Reads "machine", "rois" and "analysis" at init; restart the service after editing them.
class AnalysisModule final : public IModule {
public:
    // `config` must outlive the module.
    explicit AnalysisModule(const IConfigStore* config, AnalysisOptions options = {});
    ~AnalysisModule() override;
    AnalysisModule(const AnalysisModule&) = delete;
    AnalysisModule& operator=(const AnalysisModule&) = delete;
    AnalysisModule(AnalysisModule&&) = delete;
    AnalysisModule& operator=(AnalysisModule&&) = delete;

    [[nodiscard]] std::string_view name() const noexcept override { return "analysis"; }
    [[nodiscard]] std::vector<std::string> dependencies() const override { return {"tracking"}; }
    [[nodiscard]] Result<> init(ModuleContext& context) override;
    [[nodiscard]] Result<> start() override;
    void stop() noexcept override;
    [[nodiscard]] Health health() const override;

    // Frame sink: called on a camera grab thread, never blocks. Ignored unless running.
    void submit(const camera::Frame& frame) noexcept;

    [[nodiscard]] std::uint64_t framesAnalysed() const noexcept {
        return analysed_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t framesDropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t measurementsPublished() const noexcept {
        return published_.load(std::memory_order_relaxed);
    }

private:
    // Run time of one sensor over one log interval (diagnostics only; production statistics
    // come with their own branch and page).
    struct Window {
        std::uint64_t frames{0};
        std::uint64_t failed{0};
        double msSum{0.0};
        double msMax{0.0};
        std::map<std::string_view, double> stageMs; // sum per stage
        std::string lastError;
    };

    struct SensorRuntime {
        TrackedSensor sensor;
        AnalysisParams params;
        AnalysisPipeline pipeline;
        std::uint64_t frames{0};
        bool debugScanned{false};                     // existing debug files listed
        std::deque<std::filesystem::path> debugFiles; // oldest first
        Window window;
    };

    struct Worker {
        explicit Worker(std::size_t capacity)
            : queue{capacity} {}
        std::uint16_t cameraId{0};
        std::vector<SensorRuntime> sensors;
        BoundedQueue<camera::Frame> queue;
        std::atomic<std::uint64_t> received{0};   // frames submitted (incoming)
        std::atomic<std::uint64_t> analysedOk{0}; // frames every sensor analysed without error
        std::jthread thread;
    };

    struct RateCounts {
        std::uint64_t received{0};
        std::uint64_t analysedOk{0};
    };

    void runWorker(Worker& worker, const std::stop_token& stop);
    void analyse(Worker& worker, const camera::Frame& frame);
    void saveDebug(SensorRuntime& sensor, const camera::Frame& frame, const AnalysisContext& ctx);
    static void logWindow(std::uint16_t cameraId, SensorRuntime& sensor);
    void runJoin(const std::stop_token& stop);
    void publishRates(Timestamp::duration elapsed, std::map<std::uint16_t, RateCounts>& last);

    const IConfigStore* config_{nullptr};
    AnalysisOptions options_;
    AnalysisConfig analysisConfig_;
    MessageBus* bus_{nullptr};
    std::shared_ptr<Subscription<ObjectRecord>> records_;
    std::map<std::uint16_t, std::unique_ptr<Worker>> workers_; // by camera ID; fixed after init
    BoundedQueue<SensorResult> results_;
    std::jthread joinThread_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> analysed_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> published_{0};
    std::atomic<std::uint64_t> resultsDropped_{0};
    std::atomic<std::int64_t> lastDropNs_{0};
    std::atomic<std::uint64_t> expired_{0}; // join thread writes, health() reads
};

} // namespace vsort::service

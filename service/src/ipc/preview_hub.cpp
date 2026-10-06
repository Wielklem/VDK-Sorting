#include "ipc/preview_hub.hpp"

#include <chrono>
#include <utility>

#include <spdlog/spdlog.h>

#include "ipc/downscale.hpp"

namespace vsort::service {
namespace {

using namespace std::chrono_literals;

constexpr auto kRetireDelay = 1s; // readers get time to notice the new generation
constexpr auto kWakeInterval = 100ms;

} // namespace

PreviewHub::PreviewHub(PreviewDefaults defaults, StreamCallback onStreamChanged)
    : defaults_{defaults}
    , onStreamChanged_{std::move(onStreamChanged)} {}

PreviewHub::~PreviewHub() {
    stop();
}

Result<> PreviewHub::start() {
    const std::scoped_lock lock{mutex_};
    if (worker_.joinable()) {
        return makeError(Errc::AlreadyExists, "preview hub already started");
    }
    stopping_ = false;
    running_ = true;
    worker_ = std::thread{[this] { run(); }};
    return {};
}

void PreviewHub::stop() noexcept {
    try {
        {
            const std::scoped_lock lock{mutex_};
            stopping_ = true;
            running_ = false;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        const std::scoped_lock lock{mutex_};
        for (auto& [id, state] : cameras_) {
            state.ring.reset();
            state.pending.reset();
        }
        retired_.clear();
    } catch (...) { // NOLINT(bugprone-empty-catch): stop() must not throw
    }
}

PreviewHub::CameraState& PreviewHub::stateLocked(std::uint16_t cameraId) {
    auto [it, inserted] = cameras_.try_emplace(cameraId);
    if (inserted) {
        it->second.settings =
            PreviewSettings{.enabled = false, .fps = defaults_.fps, .maxWidth = defaults_.maxWidth};
        it->second.stream.cameraId = cameraId;
    }
    return it->second;
}

Result<PreviewSettings> PreviewHub::setPreview(std::uint16_t cameraId,
                                               const PreviewSettings& settings) {
    if (settings.fps < kMinPreviewFps || settings.fps > kMaxPreviewFps) {
        return makeError(Errc::InvalidArgument, "preview fps must be 1..60");
    }
    if (settings.maxWidth < kMinPreviewWidth || settings.maxWidth > kMaxPreviewWidth) {
        return makeError(Errc::InvalidArgument, "preview max_width must be 16..4096");
    }
    {
        const std::scoped_lock lock{mutex_};
        auto& state = stateLocked(cameraId);
        const bool wasEnabled = state.settings.enabled;
        state.settings = settings;
        if (wasEnabled && !settings.enabled) {
            state.closeRequested = true;
            state.pending.reset();
            hasWork_ = true;
        }
    }
    cv_.notify_one();
    return settings;
}

PreviewSettings PreviewHub::settings(std::uint16_t cameraId) const {
    const std::scoped_lock lock{mutex_};
    const auto it = cameras_.find(cameraId);
    if (it != cameras_.end()) {
        return it->second.settings;
    }
    return PreviewSettings{.enabled = false, .fps = defaults_.fps, .maxWidth = defaults_.maxWidth};
}

PreviewStream PreviewHub::stream(std::uint16_t cameraId) const {
    const std::scoped_lock lock{mutex_};
    const auto it = cameras_.find(cameraId);
    if (it != cameras_.end()) {
        return it->second.stream;
    }
    PreviewStream none;
    none.cameraId = cameraId;
    return none;
}

PreviewCounters PreviewHub::counters() const {
    return PreviewCounters{.offered = offered_.load(),
                           .skipped = skipped_.load(),
                           .replaced = replaced_.load(),
                           .published = published_.load(),
                           .errors = errors_.load()};
}

void PreviewHub::submit(const camera::Frame& frame) noexcept {
    if (!running_.load(std::memory_order_relaxed)) {
        return;
    }
    try {
        const std::uint16_t id = frame.meta.cameraIndex;
        const Timestamp now = Timestamp::now();
        {
            const std::scoped_lock lock{mutex_};
            const auto it = cameras_.find(id);
            if (it == cameras_.end() || !it->second.settings.enabled) {
                return;
            }
            auto& state = it->second;
            offered_.fetch_add(1, std::memory_order_relaxed);
            const auto interval = std::chrono::nanoseconds{1'000'000'000LL / state.settings.fps};
            if (state.hasLast && (now - state.lastAccepted) < interval) {
                skipped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            state.lastAccepted = now;
            state.hasLast = true;
        }

        // Without an owner the pixels are only valid during this callback: keep a copy.
        camera::Frame kept = frame;
        if (!kept.owner) {
            auto copy =
                std::make_shared<std::vector<std::byte>>(frame.data.begin(), frame.data.end());
            kept.data = *copy;
            kept.owner = copy;
        }
        {
            const std::scoped_lock lock{mutex_};
            const auto it = cameras_.find(id);
            if (it == cameras_.end() || !it->second.settings.enabled) {
                return;
            }
            if (it->second.pending) {
                replaced_.fetch_add(1, std::memory_order_relaxed);
            }
            it->second.pending = std::move(kept);
            hasWork_ = true;
        }
        cv_.notify_one();
    } catch (...) { // NOLINT(bugprone-empty-catch): grab thread callback must not throw
    }
}

void PreviewHub::run() {
    std::unique_lock lock{mutex_};
    while (!stopping_) {
        cv_.wait_for(lock, kWakeInterval, [this] { return stopping_ || hasWork_; });
        if (stopping_) {
            break;
        }
        hasWork_ = false;

        std::vector<Job> jobs;
        std::vector<std::pair<std::uint16_t, CameraState*>> toClose;
        for (auto& [id, state] : cameras_) {
            if (state.closeRequested) {
                state.closeRequested = false;
                toClose.emplace_back(id, &state);
            }
            if (state.pending) {
                jobs.push_back(Job{id, &state, std::move(*state.pending), state.settings});
                state.pending.reset();
            }
        }
        lock.unlock();

        for (const auto& [id, state] : toClose) {
            closeRing(id, *state);
        }
        for (const auto& job : jobs) {
            process(job);
        }
        reapRetired(false);

        lock.lock();
    }
}

void PreviewHub::process(const Job& job) {
    CameraState& state = *job.state;
    auto image = downscaleForPreview(job.frame, job.settings.maxWidth);
    if (!image) {
        noteError(job.cameraId, image.error());
        return;
    }

    const bool needNewRing = !state.ring || state.stream.width != image->width ||
                             state.stream.height != image->height ||
                             state.stream.format != image->format;
    if (needNewRing) {
        const std::uint32_t generation = state.generation + 1;
        const auto name = ipc::previewRingName(job.cameraId, generation);
        auto ring = ipc::PreviewRingWriter::create(
            name, ipc::RingSpec{.slotCount = defaults_.slotCount,
                                .slotSize = static_cast<std::uint32_t>(image->pixels.size()),
                                .generation = generation});
        if (!ring) {
            noteError(job.cameraId, ring.error());
            return;
        }
        state.generation = generation;
        retire(std::move(state.ring));
        state.ring = std::move(*ring);

        PreviewStream info{.cameraId = job.cameraId,
                           .shmName = name,
                           .generation = generation,
                           .width = image->width,
                           .height = image->height,
                           .format = image->format};
        {
            const std::scoped_lock lock{mutex_};
            state.stream = info;
        }
        if (onStreamChanged_) {
            onStreamChanged_(info);
        }
    }

    const ipc::PreviewFrameInfo info{
        .frameId = job.frame.meta.frameId.value(),
        .timestampNs = static_cast<std::uint64_t>(job.frame.meta.hostTimestamp.ns()),
        .width = image->width,
        .height = image->height,
        .strideBytes = image->width * ipc::bytesPerPixel(image->format),
        .pixelFormat = image->format};
    if (const auto written = state.ring->write(info, image->pixels); !written) {
        noteError(job.cameraId, written.error());
        return;
    }
    published_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewHub::closeRing(std::uint16_t cameraId, CameraState& state) {
    if (!state.ring) {
        return;
    }
    retire(std::move(state.ring));
    PreviewStream closed;
    {
        const std::scoped_lock lock{mutex_};
        closed = PreviewStream{.cameraId = cameraId,
                               .shmName = {},
                               .generation = state.generation,
                               .width = 0,
                               .height = 0,
                               .format = state.stream.format};
        state.stream = closed;
    }
    if (onStreamChanged_) {
        onStreamChanged_(closed);
    }
}

void PreviewHub::retire(std::unique_ptr<ipc::PreviewRingWriter> ring) {
    if (ring) {
        retired_.push_back(Retired{std::move(ring), Timestamp::now() + kRetireDelay});
    }
}

void PreviewHub::reapRetired(bool all) {
    const Timestamp now = Timestamp::now();
    std::erase_if(retired_, [&](const Retired& r) { return all || r.closeAt <= now; });
}

void PreviewHub::noteError(std::uint16_t cameraId, const Error& error) {
    const std::uint64_t n = errors_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 1 || n % 1000 == 0) { // rate limited
        spdlog::warn("preview camera {}: {} (error #{})", cameraId, error.what(), n);
    }
}

} // namespace vsort::service

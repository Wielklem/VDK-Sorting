#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <vsort/common/logging.hpp>
#include <vsort/common/timestamp.hpp>
#include <vsort/recorder/recording_format.hpp>
#include <vsort/replay/replay_camera.hpp>

namespace vsort::replay {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr std::int64_t kFallbackIntervalNs = 1'000'000; // loop seam when the interval is unknown
constexpr std::uint8_t kMinPixelFormat = 1;
constexpr std::uint8_t kMaxPixelFormat = 4;

struct IndexEntry {
    std::uint64_t payloadOffset{0};
    recorder::RecordHeader header;
};

// What open() learns from the recording. Immutable while streaming.
struct Recording {
    std::uint16_t cameraIndex{0};
    std::vector<IndexEntry> frames;
    bool truncated{false};
    std::int64_t firstNs{0};
    std::int64_t loopPeriodNs{0}; // recorded span plus one average interval
    std::uint64_t idPeriod{0};    // frame-ID step per pass, keeps IDs increasing
};

[[nodiscard]] bool validSpeed(double speed) noexcept {
    return std::isfinite(speed) && speed > 0.0;
}

// Reads <dir>/session.json and finds the camera with this serial.
struct SessionCamera {
    std::uint16_t index{0};
    std::string serial;
    std::string model;
    std::string file;
    bool complete{false};
};

Result<SessionCamera> findCamera(const fs::path& sessionDir, std::string_view serial) {
    const fs::path path = sessionDir / "session.json";
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return makeError(Errc::NotFound, std::format("cannot open '{}'", path.string()));
    }
    try {
        const nlohmann::json session = nlohmann::json::parse(in, nullptr, false);
        if (session.is_discarded() || !session.is_object() || !session.contains("cameras") ||
            !session["cameras"].is_array()) {
            return makeError(Errc::ParseError, std::format("damaged '{}'", path.string()));
        }
        const bool complete = session.value("status", std::string{}) == "complete";
        for (const auto& c : session["cameras"]) {
            if (!c.is_object() || c.value("serial", std::string{}) != serial) {
                continue;
            }
            SessionCamera found;
            found.index = c.at("index").get<std::uint16_t>();
            found.serial = std::string{serial};
            found.model = c.value("model", std::string{});
            found.file = c.at("file").get<std::string>();
            found.complete = complete;
            // The file must sit next to session.json, so a dataset cannot point elsewhere.
            if (found.file.empty() || fs::path{found.file}.filename() != fs::path{found.file}) {
                return makeError(Errc::ParseError, "session.json: bad file name");
            }
            return found;
        }
    } catch (const nlohmann::json::exception& e) {
        return makeError(Errc::ParseError,
                         std::format("damaged '{}': {}", path.string(), e.what()));
    }
    return makeError(Errc::NotFound,
                     std::format("serial '{}' is not in '{}'", serial, path.string()));
}

// Builds the frame index with one pass over the headers. The pixel data is not read here.
Result<Recording> indexFile(std::ifstream& in, const fs::path& path, std::uint16_t expectedIndex) {
    std::error_code ec;
    const std::uintmax_t fileSize = fs::file_size(path, ec);
    if (ec) {
        return makeError(Errc::IoError,
                         std::format("cannot size '{}': {}", path.string(), ec.message()));
    }

    recorder::FileHeaderBytes fileHeaderBytes{};
    in.read(reinterpret_cast<char*>(fileHeaderBytes.data()),
            static_cast<std::streamsize>(fileHeaderBytes.size()));
    const auto fileHeader = in ? recorder::decodeFileHeader(fileHeaderBytes) : std::nullopt;
    if (!fileHeader) {
        return makeError(Errc::ParseError, std::format("'{}' is not a recording", path.string()));
    }
    if (fileHeader->version != recorder::kFormatVersion) {
        return makeError(Errc::NotSupported, std::format("'{}': format version {}", path.string(),
                                                         fileHeader->version));
    }
    if (fileHeader->cameraIndex != expectedIndex) {
        return makeError(
            Errc::ParseError,
            std::format("'{}': camera index differs from session.json", path.string()));
    }

    Recording rec;
    rec.cameraIndex = fileHeader->cameraIndex;
    std::uint64_t offset = recorder::kFileHeaderSize;
    while (offset + recorder::kRecordHeaderSize <= fileSize) {
        recorder::RecordHeaderBytes bytes{};
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        const auto header = in ? recorder::decodeRecordHeader(bytes) : std::nullopt;
        if (!header) {
            return makeError(Errc::IoError, std::format("cannot read '{}'", path.string()));
        }
        const std::uint64_t payloadOffset = offset + recorder::kRecordHeaderSize;
        if (payloadOffset + header->payloadBytes > fileSize) {
            break; // half-written last frame
        }
        const bool formatOk =
            header->pixelFormat >= kMinPixelFormat && header->pixelFormat <= kMaxPixelFormat;
        const bool sizeOk =
            header->width > 0 && header->height > 0 &&
            header->payloadBytes >= std::uint64_t{header->strideBytes} * header->height;
        if (!formatOk || !sizeOk) {
            return makeError(Errc::ParseError, std::format("'{}': invalid frame record {}",
                                                           path.string(), rec.frames.size()));
        }
        rec.frames.push_back(IndexEntry{.payloadOffset = payloadOffset, .header = *header});
        offset = payloadOffset + header->payloadBytes;
    }
    rec.truncated = offset != fileSize;
    if (rec.frames.empty()) {
        return makeError(Errc::NotFound, std::format("'{}' has no frames", path.string()));
    }

    const auto& first = rec.frames.front().header;
    const auto& last = rec.frames.back().header;
    rec.firstNs = first.hostTimestampNs;
    const std::int64_t spanNs =
        std::max<std::int64_t>(0, last.hostTimestampNs - first.hostTimestampNs);
    const auto gaps = static_cast<std::int64_t>(rec.frames.size() - 1);
    const std::int64_t meanNs = gaps > 0 ? spanNs / gaps : 0;
    rec.loopPeriodNs = spanNs + (meanNs > 0 ? meanNs : kFallbackIntervalNs);
    rec.idPeriod = last.frameId >= first.frameId ? last.frameId - first.frameId + 1
                                                 : static_cast<std::uint64_t>(rec.frames.size());
    return rec;
}

} // namespace

struct ReplayCamera::Impl {
    explicit Impl(ReplayConfig c)
        : config{std::move(c)}
        , speed{config.speed} {}

    const ReplayConfig config;

    // Set by open(), cleared by close(). The play thread only reads them while streaming.
    std::atomic<bool> open{false};
    camera::CameraInfo info;
    camera::CameraSettings settings;
    Recording recording;
    std::ifstream file;

    camera::FrameCallback onFrame;
    camera::CameraEventCallback onEvent;

    // Play clock. Media time 0 is the first frame of the first pass.
    mutable std::mutex mutex;
    std::condition_variable wake;
    mutable std::condition_variable done;
    double speed;
    Clock::time_point anchorWall;
    std::int64_t anchorMediaNs{0};
    std::uint64_t generation{0}; // bumped by setSpeed() to interrupt a wait
    bool stopRequested{false};
    bool finished{false};

    std::atomic<bool> streaming{false};
    std::atomic<std::uint64_t> delivered{0};
    std::atomic<std::uint64_t> failed{0};
    std::atomic<std::uint64_t> loops{0};
    std::thread thread; // last: destroyed first

    [[nodiscard]] Clock::time_point dueTime(std::int64_t mediaNs) const {
        const double deltaNs = static_cast<double>(mediaNs - anchorMediaNs) / speed;
        return anchorWall + std::chrono::duration_cast<Clock::duration>(
                                std::chrono::duration<double, std::nano>{deltaNs});
    }

    // Blocks until the frame is due. False if stop() was requested meanwhile.
    [[nodiscard]] bool waitUntilDue(std::int64_t mediaNs) {
        std::unique_lock lock{mutex};
        for (;;) {
            if (stopRequested) {
                return false;
            }
            const auto due = dueTime(mediaNs);
            if (Clock::now() >= due) {
                return true;
            }
            const auto seen = generation;
            wake.wait_until(lock, due, [&] { return stopRequested || generation != seen; });
        }
    }

    [[nodiscard]] bool readPayload(const IndexEntry& entry, std::vector<std::byte>& out) {
        file.clear();
        file.seekg(static_cast<std::streamoff>(entry.payloadOffset));
        file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        return static_cast<bool>(file);
    }

    void report(camera::CameraEventKind kind, std::string_view detail) noexcept {
        try {
            if (onEvent) {
                onEvent(camera::CameraEvent{.kind = kind, .detail = detail});
            }
        } catch (...) {
            // Callbacks must not throw; nothing sensible to do if one does.
        }
    }

    void deliver(const IndexEntry& entry, std::uint64_t pass,
                 std::shared_ptr<std::vector<std::byte>> pixels) noexcept {
        try {
            const auto& h = entry.header;
            camera::Frame frame;
            frame.meta = camera::FrameMetadata{
                .frameId = FrameId{h.frameId + pass * recording.idPeriod},
                .hostTimestamp = Timestamp::now(),
                .deviceTimestampNs = h.deviceTimestampNs == 0
                                         ? 0
                                         : h.deviceTimestampNs + pass * static_cast<std::uint64_t>(
                                                                            recording.loopPeriodNs),
                .width = h.width,
                .height = h.height,
                .strideBytes = h.strideBytes,
                .pixelFormat = static_cast<camera::PixelFormat>(h.pixelFormat),
                .cameraIndex = recording.cameraIndex};
            frame.data = std::span<const std::byte>{*pixels};
            frame.owner = std::move(pixels);
            if (onFrame) {
                onFrame(frame);
            }
            delivered.fetch_add(1, std::memory_order_relaxed);
        } catch (...) {
            failed.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Returns when the end is reached (not looping) or stop() is requested.
    void play() {
        for (std::uint64_t pass = 0;; ++pass) {
            for (const auto& entry : recording.frames) {
                // Read first, so the disk read does not delay the frame.
                auto pixels = std::make_shared<std::vector<std::byte>>(entry.header.payloadBytes);
                const bool ok = readPayload(entry, *pixels);

                const std::int64_t mediaNs =
                    std::max<std::int64_t>(0, entry.header.hostTimestampNs - recording.firstNs) +
                    static_cast<std::int64_t>(pass) * recording.loopPeriodNs;
                if (!waitUntilDue(mediaNs)) {
                    return;
                }
                if (!ok) {
                    failed.fetch_add(1, std::memory_order_relaxed);
                    report(camera::CameraEventKind::FrameDropped, "replay: cannot read frame data");
                    continue;
                }
                deliver(entry, pass, std::move(pixels));
            }
            loops.fetch_add(1, std::memory_order_relaxed);
            if (!config.loop) {
                return;
            }
        }
    }

    void run() noexcept {
        bool completed = false;
        try {
            play();
            std::scoped_lock lock{mutex};
            completed = !stopRequested;
        } catch (...) {
            log::get("replay")->error("replay thread stopped by an exception");
        }
        {
            std::scoped_lock lock{mutex};
            finished = completed && !config.loop;
        }
        done.notify_all();
    }
};

ReplayCamera::ReplayCamera(ReplayConfig config)
    : impl_{std::make_unique<Impl>(std::move(config))} {}

ReplayCamera::~ReplayCamera() {
    close();
}

Result<> ReplayCamera::open(std::string_view serial) {
    Impl& im = *impl_;
    if (serial.empty()) {
        return makeError(Errc::InvalidArgument, "empty serial");
    }
    if (im.config.sessionDir.empty()) {
        return makeError(Errc::InvalidArgument, "ReplayCamera: sessionDir is empty");
    }
    if (!validSpeed(speed())) {
        return makeError(Errc::InvalidArgument, "ReplayCamera: speed must be finite and > 0");
    }
    if (im.open) {
        return makeError(Errc::AlreadyExists, "camera already open");
    }

    auto found = findCamera(im.config.sessionDir, serial);
    if (!found) {
        return std::unexpected{std::move(found.error())};
    }
    const fs::path path = im.config.sessionDir / found->file;
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return makeError(Errc::IoError, std::format("cannot open '{}'", path.string()));
    }
    auto recording = indexFile(in, path, found->index);
    if (!recording) {
        return std::unexpected{std::move(recording.error())};
    }

    im.file = std::move(in);
    im.recording = std::move(*recording);
    const auto& first = im.recording.frames.front().header;
    im.info = camera::CameraInfo{.serial = found->serial,
                                 .model = found->model,
                                 .sensorWidth = first.width,
                                 .sensorHeight = first.height};
    im.settings = camera::CameraSettings{};
    im.open = true;

    auto logger = log::get("replay");
    logger->info("opened {}: {} frames, camera {}", path.string(), im.recording.frames.size(),
                 im.recording.cameraIndex);
    if (!found->complete || im.recording.truncated) {
        logger->warn("{} is not a complete recording; playing what is there", path.string());
    }
    return {};
}

void ReplayCamera::close() noexcept {
    Impl& im = *impl_;
    stop();
    im.open = false;
    im.file.close();
    im.recording = Recording{};
}

bool ReplayCamera::isOpen() const noexcept {
    return impl_->open;
}

Result<camera::CameraInfo> ReplayCamera::info() const {
    if (!impl_->open) {
        return makeError(Errc::NotFound, "not open");
    }
    return impl_->info;
}

Result<camera::CameraSettings> ReplayCamera::settings() const {
    if (!impl_->open) {
        return makeError(Errc::NotFound, "not open");
    }
    return impl_->settings;
}

Result<> ReplayCamera::applySettings(const camera::CameraSettings& settings) {
    Impl& im = *impl_;
    if (!im.open) {
        return makeError(Errc::NotFound, "not open");
    }
    if (!std::isfinite(settings.exposureUs) || settings.exposureUs <= 0.0) {
        return makeError(Errc::InvalidArgument, "exposure must be > 0");
    }
    if (!std::isfinite(settings.gainDb) || settings.gainDb < 0.0) {
        return makeError(Errc::InvalidArgument, "gain must be >= 0");
    }
    const camera::Roi& r = settings.roi;
    if ((r.width == 0) != (r.height == 0)) {
        return makeError(Errc::InvalidArgument, "ROI width and height must both be 0 or both set");
    }
    if (r.width != 0) {
        return makeError(Errc::NotSupported, "replay camera cannot crop a ROI");
    }
    im.settings = settings;
    return {};
}

void ReplayCamera::setFrameCallback(camera::FrameCallback callback) {
    impl_->onFrame = std::move(callback);
}

void ReplayCamera::setEventCallback(camera::CameraEventCallback callback) {
    impl_->onEvent = std::move(callback);
}

Result<> ReplayCamera::start() {
    Impl& im = *impl_;
    if (!im.open) {
        return makeError(Errc::NotFound, "not open");
    }
    if (im.streaming) {
        return makeError(Errc::AlreadyExists, "already streaming");
    }
    {
        std::scoped_lock lock{im.mutex};
        im.anchorWall = Clock::now();
        im.anchorMediaNs = 0;
        im.stopRequested = false;
        im.finished = false;
    }
    im.delivered = 0;
    im.failed = 0;
    im.loops = 0;
    try {
        im.thread = std::thread{[&im] { im.run(); }};
    } catch (const std::system_error& e) {
        return makeError(Errc::Internal, std::format("cannot start replay thread: {}", e.what()));
    }
    im.streaming = true;
    return {};
}

void ReplayCamera::stop() noexcept {
    Impl& im = *impl_;
    {
        std::scoped_lock lock{im.mutex};
        im.stopRequested = true;
    }
    im.wake.notify_all();
    if (im.thread.joinable()) {
        im.thread.join();
    }
    im.streaming = false;
}

bool ReplayCamera::isStreaming() const noexcept {
    return impl_->streaming;
}

Result<> ReplayCamera::setSpeed(double speed) {
    if (!validSpeed(speed)) {
        return makeError(Errc::InvalidArgument, "speed must be finite and > 0");
    }
    Impl& im = *impl_;
    {
        std::scoped_lock lock{im.mutex};
        // Re-anchor at the current position, so the speed change does not jump in time.
        const auto now = Clock::now();
        const double elapsedNs =
            std::chrono::duration<double, std::nano>{now - im.anchorWall}.count();
        im.anchorMediaNs += static_cast<std::int64_t>(elapsedNs * im.speed);
        im.anchorWall = now;
        im.speed = speed;
        ++im.generation;
    }
    im.wake.notify_all();
    return {};
}

double ReplayCamera::speed() const noexcept {
    std::scoped_lock lock{impl_->mutex};
    return impl_->speed;
}

std::uint64_t ReplayCamera::frameCount() const noexcept {
    return impl_->open ? impl_->recording.frames.size() : 0;
}

ReplayStats ReplayCamera::stats() const noexcept {
    const Impl& im = *impl_;
    std::scoped_lock lock{im.mutex};
    return ReplayStats{.framesDelivered = im.delivered.load(),
                       .framesFailed = im.failed.load(),
                       .loops = im.loops.load(),
                       .finished = im.finished};
}

bool ReplayCamera::waitUntilFinished(std::chrono::milliseconds timeout) const {
    const Impl& im = *impl_;
    std::unique_lock lock{im.mutex};
    return im.done.wait_for(lock, timeout, [&] { return im.finished; });
}

} // namespace vsort::replay

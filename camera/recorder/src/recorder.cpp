#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <format>
#include <fstream>
#include <limits>
#include <mutex>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#include <vsort/common/bounded_queue.hpp>
#include <vsort/common/logging.hpp>
#include <vsort/common/timestamp.hpp>
#include <vsort/recorder/recorder.hpp>
#include <vsort/recorder/recording_format.hpp>

namespace vsort::recorder {
namespace {

namespace fs = std::filesystem;

constexpr auto kWakeInterval = std::chrono::milliseconds{10};
constexpr std::uint64_t kLogEvery = 1000;
constexpr int kMaxSessionSuffix = 100;

struct QueuedFrame {
    camera::FrameMetadata meta;
    std::span<const std::byte> data;
    std::shared_ptr<const void> owner; // keeps the pool buffer alive until written
};

struct Stream {
    CameraStream camera;
    std::string fileName;
    std::ofstream file;
    std::uint64_t frames{0};
    std::uint64_t firstFrameId{0};
    std::uint64_t lastFrameId{0};
};

std::string cameraFileName(std::uint16_t index) {
    return std::format("cam{:02}.vrec", index);
}

// 2026-10-03T12:34:56.123456Z -> 20261003T123456 (no ':' so it is a valid folder name everywhere)
std::string sessionName(const WallTime& time) {
    const std::string iso = time.toIso8601();
    std::string name;
    for (std::size_t i = 0; i < 19 && i < iso.size(); ++i) {
        if (iso[i] != '-' && iso[i] != ':') {
            name += iso[i];
        }
    }
    return name;
}

Result<fs::path> createSessionDirectory(const fs::path& root, const std::string& base) {
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        return makeError(Errc::IoError,
                         std::format("cannot create '{}': {}", root.string(), ec.message()));
    }
    for (int attempt = 1; attempt <= kMaxSessionSuffix; ++attempt) {
        const std::string name = attempt == 1 ? base : std::format("{}-{}", base, attempt);
        fs::path candidate = root / name;
        if (fs::create_directory(candidate, ec)) {
            return candidate;
        }
        if (ec) {
            return makeError(Errc::IoError, std::format("cannot create '{}': {}",
                                                        candidate.string(), ec.message()));
        }
    }
    return makeError(Errc::AlreadyExists, "no free session folder name");
}

// Write to <path>.tmp, then rename, so a crash never leaves a half-written file.
Result<> writeTextAtomically(const fs::path& path, const std::string& text) {
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out{tmp, std::ios::binary | std::ios::trunc};
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            return makeError(Errc::IoError, std::format("cannot write '{}'", tmp.string()));
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        return makeError(Errc::IoError,
                         std::format("cannot rename to '{}': {}", path.string(), ec.message()));
    }
    return {};
}

} // namespace

struct Recorder::Impl {
    explicit Impl(std::size_t depth)
        : queue{depth} {}

    fs::path sessionDir;
    std::string label;
    std::string startedAt;
    std::vector<std::unique_ptr<Stream>>
        streams; // fixed after start(); writer thread owns the files
    BoundedQueue<QueuedFrame> queue;
    std::mutex wakeMutex;
    std::condition_variable wake;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> finished{false};
    std::atomic<std::uint64_t> written{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> failed{0};
    std::thread writer; // last: destroyed first

    Stream* findStream(std::uint16_t cameraIndex) const noexcept {
        for (const auto& s : streams) {
            if (s->camera.cameraIndex == cameraIndex) {
                return s.get();
            }
        }
        return nullptr;
    }

    // Rate limited: first failure, then every 1000th.
    void noteFailure(const std::string& message) noexcept {
        const auto n = failed.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n != 1 && n % kLogEvery != 0) {
            return;
        }
        try {
            log::get("recorder")->warn("{} ({} failures so far)", message, n);
        } catch (...) {
            // Logging must never take the writer down.
        }
    }

    void writeFrame(const QueuedFrame& f) noexcept {
        try {
            Stream* s = findStream(f.meta.cameraIndex);
            if (s == nullptr) {
                noteFailure("frame for unknown camera index " + std::to_string(f.meta.cameraIndex));
                return;
            }
            if (f.data.size() > std::numeric_limits<std::uint32_t>::max()) {
                noteFailure("frame too large");
                return;
            }
            const auto header = encodeRecordHeader(
                RecordHeader{.frameId = f.meta.frameId.value(),
                             .hostTimestampNs = f.meta.hostTimestamp.ns(),
                             .deviceTimestampNs = f.meta.deviceTimestampNs,
                             .width = f.meta.width,
                             .height = f.meta.height,
                             .strideBytes = f.meta.strideBytes,
                             .payloadBytes = static_cast<std::uint32_t>(f.data.size()),
                             .pixelFormat = static_cast<std::uint8_t>(f.meta.pixelFormat)});
            s->file.write(reinterpret_cast<const char*>(header.data()),
                          static_cast<std::streamsize>(header.size()));
            s->file.write(reinterpret_cast<const char*>(f.data.data()),
                          static_cast<std::streamsize>(f.data.size()));
            if (!s->file) {
                noteFailure("write failed: " + s->fileName);
                return;
            }
            if (s->frames == 0) {
                s->firstFrameId = f.meta.frameId.value();
            }
            s->lastFrameId = f.meta.frameId.value();
            ++s->frames;
            written.fetch_add(1, std::memory_order_relaxed);
        } catch (...) {
            failed.fetch_add(1, std::memory_order_relaxed);
        }
    }

    bool drain() noexcept {
        bool any = false;
        while (auto item = queue.tryPop()) {
            writeFrame(*item);
            any = true;
        }
        return any;
    }

    void run() noexcept {
        for (;;) {
            const bool worked = drain();
            if (stopRequested.load(std::memory_order_acquire)) {
                drain(); // frames pushed just before the stop request
                return;
            }
            if (!worked) {
                std::unique_lock lock{wakeMutex};
                wake.wait_for(lock, kWakeInterval); // a missed notify costs at most this long
            }
        }
    }

    [[nodiscard]] nlohmann::json describe(std::string_view status,
                                          const std::string& finishedAt) const {
        auto cameras = nlohmann::json::array();
        for (const auto& s : streams) {
            cameras.push_back(nlohmann::json{{"index", s->camera.cameraIndex},
                                             {"serial", s->camera.serial},
                                             {"model", s->camera.model},
                                             {"file", s->fileName},
                                             {"frames", s->frames},
                                             {"first_frame_id", s->firstFrameId},
                                             {"last_frame_id", s->lastFrameId}});
        }
        nlohmann::json j;
        j["format_version"] = kFormatVersion;
        j["status"] = std::string{status};
        j["label"] = label;
        j["started_at"] = startedAt;
        j["finished_at"] = finishedAt;
        j["cameras"] = std::move(cameras);
        j["frames_written"] = written.load();
        j["frames_dropped"] = dropped.load();
        j["frames_failed"] = failed.load();
        return j;
    }
};

Result<std::unique_ptr<Recorder>> Recorder::start(const RecorderConfig& config,
                                                  std::vector<CameraStream> cameras) {
    if (config.rootDir.empty()) {
        return makeError(Errc::InvalidArgument, "Recorder: rootDir is empty");
    }
    if (config.queueDepth == 0) {
        return makeError(Errc::InvalidArgument, "Recorder: queueDepth is 0");
    }
    if (cameras.empty()) {
        return makeError(Errc::InvalidArgument, "Recorder: no cameras");
    }
    std::vector<std::uint16_t> indices;
    for (const auto& c : cameras) {
        indices.push_back(c.cameraIndex);
    }
    std::ranges::sort(indices);
    if (std::ranges::adjacent_find(indices) != indices.end()) {
        return makeError(Errc::InvalidArgument, "Recorder: duplicate camera index");
    }

    const WallTime now = WallTime::now();
    auto dir = createSessionDirectory(config.rootDir, sessionName(now));
    if (!dir) {
        return std::unexpected{std::move(dir.error())};
    }

    auto impl = std::make_unique<Impl>(config.queueDepth);
    impl->sessionDir = std::move(*dir);
    impl->label = config.label;
    impl->startedAt = now.toIso8601();

    for (auto& camera : cameras) {
        auto stream = std::make_unique<Stream>();
        stream->fileName = cameraFileName(camera.cameraIndex);
        stream->camera = std::move(camera);
        const fs::path path = impl->sessionDir / stream->fileName;
        stream->file.open(path, std::ios::binary | std::ios::trunc);
        const auto header = encodeFileHeader(FileHeader{.cameraIndex = stream->camera.cameraIndex});
        stream->file.write(reinterpret_cast<const char*>(header.data()),
                           static_cast<std::streamsize>(header.size()));
        if (!stream->file) {
            return makeError(Errc::IoError, std::format("cannot write '{}'", path.string()));
        }
        impl->streams.push_back(std::move(stream));
    }

    if (auto r = writeTextAtomically(impl->sessionDir / "session.json",
                                     impl->describe("recording", "").dump(2));
        !r) {
        return std::unexpected{std::move(r.error())};
    }

    try {
        impl->writer = std::thread{[raw = impl.get()] { raw->run(); }};
    } catch (const std::system_error& e) {
        return makeError(Errc::Internal,
                         std::format("Recorder: cannot start writer: {}", e.what()));
    }
    log::get("recorder")->info("recording to {}", impl->sessionDir.string());
    return std::unique_ptr<Recorder>{new Recorder{std::move(impl)}};
}

Recorder::Recorder(std::unique_ptr<Impl> impl) noexcept
    : impl_{std::move(impl)} {}

Recorder::~Recorder() {
    try {
        static_cast<void>(finish());
    } catch (...) {
        // Nothing sensible left to do in a destructor.
    }
}

void Recorder::submit(const camera::Frame& frame) noexcept {
    Impl& im = *impl_;
    try {
        if (im.stopRequested.load(std::memory_order_acquire)) {
            im.dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!im.queue.tryPush(QueuedFrame{frame.meta, frame.data, frame.owner})) {
            im.dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        im.wake.notify_one();
    } catch (...) {
        im.failed.fetch_add(1, std::memory_order_relaxed);
    }
}

camera::FrameCallback Recorder::callbackFor(std::uint16_t cameraIndex) {
    return [this, cameraIndex](const camera::Frame& frame) {
        camera::Frame tagged = frame;
        tagged.meta.cameraIndex = cameraIndex;
        submit(tagged);
    };
}

Result<> Recorder::finish() {
    Impl& im = *impl_;
    if (im.finished.exchange(true)) {
        return {};
    }
    im.stopRequested.store(true, std::memory_order_release);
    im.wake.notify_all();
    if (im.writer.joinable()) {
        im.writer.join();
    }

    bool filesOk = true;
    for (const auto& s : im.streams) {
        s->file.flush();
        s->file.close();
        filesOk = filesOk && !s->file.fail();
    }

    auto r = writeTextAtomically(im.sessionDir / "session.json",
                                 im.describe("complete", WallTime::now().toIso8601()).dump(2));
    log::get("recorder")
        ->info("session closed: {} frames written, {} dropped, {} failed", im.written.load(),
               im.dropped.load(), im.failed.load());
    if (!r) {
        return r;
    }
    if (!filesOk) {
        return makeError(Errc::IoError, "Recorder: error while writing frame files");
    }
    return {};
}

const std::filesystem::path& Recorder::sessionDir() const noexcept {
    return impl_->sessionDir;
}

RecorderStats Recorder::stats() const noexcept {
    return RecorderStats{.written = impl_->written.load(),
                         .dropped = impl_->dropped.load(),
                         .failed = impl_->failed.load()};
}

} // namespace vsort::recorder

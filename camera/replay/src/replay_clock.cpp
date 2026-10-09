// REPLAY ONLY: see replay_clock.hpp.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include <vsort/recorder/recording_format.hpp>
#include <vsort/replay/replay_clock.hpp>
#include <vsort/replay/replay_session.hpp>

namespace vsort::replay {
namespace {

namespace fs = std::filesystem;

constexpr std::int64_t kFallbackIntervalNs = 1'000'000; // as for a single camera

struct Span {
    std::int64_t firstNs{0};
    std::int64_t lastNs{0};
    std::uint64_t frames{0};
};

// First and last recorded host time of one camera, from the record headers only. A half-written
// last frame is left out, as the camera does. nullopt: unreadable or no frames.
std::optional<Span> scan(const fs::path& path) {
    std::error_code ec;
    const std::uintmax_t fileSize = fs::file_size(path, ec);
    std::ifstream in{path, std::ios::binary};
    if (ec || !in) {
        return std::nullopt;
    }
    recorder::FileHeaderBytes fileHeader{};
    in.read(reinterpret_cast<char*>(fileHeader.data()),
            static_cast<std::streamsize>(fileHeader.size()));
    if (!in || !recorder::decodeFileHeader(fileHeader)) {
        return std::nullopt;
    }
    Span span;
    std::uint64_t offset = recorder::kFileHeaderSize;
    while (offset + recorder::kRecordHeaderSize <= fileSize) {
        recorder::RecordHeaderBytes bytes{};
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        const auto header = in ? recorder::decodeRecordHeader(bytes) : std::nullopt;
        const std::uint64_t payloadOffset = offset + recorder::kRecordHeaderSize;
        if (!header || payloadOffset + header->payloadBytes > fileSize) {
            break;
        }
        if (span.frames == 0) {
            span.firstNs = header->hostTimestampNs;
        }
        span.lastNs = header->hostTimestampNs;
        ++span.frames;
        offset = payloadOffset + header->payloadBytes;
    }
    if (span.frames == 0) {
        return std::nullopt;
    }
    return span;
}

} // namespace

ReplayClock::ReplayClock(std::int64_t firstNs, std::int64_t loopPeriodNs, double speed,
                         std::chrono::nanoseconds startDelay)
    : firstNs_{firstNs}
    , loopPeriodNs_{std::max<std::int64_t>(loopPeriodNs, 1)}
    , startDelay_{startDelay}
    , speed_{speed}
    , anchorWall_{Clock::now()} {}

Result<std::shared_ptr<ReplayClock>> ReplayClock::forSession(const fs::path& sessionDir,
                                                             double speed) {
    if (!std::isfinite(speed) || speed <= 0.0) {
        return makeError(Errc::InvalidArgument, "ReplayClock: speed must be finite and > 0");
    }
    auto cameras = listSessionCameras(sessionDir);
    if (!cameras) {
        return std::unexpected{std::move(cameras.error())};
    }
    std::optional<Span> session;
    double intervalSum = 0.0;
    std::size_t intervalCount = 0;
    for (const auto& cam : *cameras) {
        // Same rule as the camera: the file must sit next to session.json.
        if (cam.file.empty() || fs::path{cam.file}.filename() != fs::path{cam.file}) {
            continue;
        }
        const auto span = scan(sessionDir / cam.file);
        if (!span) {
            continue; // no frames (camera got no triggers): nothing to align
        }
        if (!session) {
            session = *span;
        } else {
            session->firstNs = std::min(session->firstNs, span->firstNs);
            session->lastNs = std::max(session->lastNs, span->lastNs);
        }
        if (span->frames > 1 && span->lastNs > span->firstNs) {
            intervalSum += static_cast<double>(span->lastNs - span->firstNs) /
                           static_cast<double>(span->frames - 1);
            ++intervalCount;
        }
    }
    if (!session) {
        return makeError(Errc::NotFound, "session '" + sessionDir.string() + "' has no frames");
    }
    const std::int64_t meanNs =
        intervalCount > 0 ? static_cast<std::int64_t>(
                                std::llround(intervalSum / static_cast<double>(intervalCount)))
                          : kFallbackIntervalNs;
    const std::int64_t spanNs = std::max<std::int64_t>(0, session->lastNs - session->firstNs);
    return std::make_shared<ReplayClock>(
        session->firstNs, spanNs + std::max<std::int64_t>(meanNs, 1), speed, kSessionStartDelay);
}

std::int64_t ReplayClock::attach() {
    std::scoped_lock lock{mutex_};
    const auto now = Clock::now();
    if (attached_++ == 0) {
        anchorWall_ = now + startDelay_;
        anchorMediaNs_ = 0;
        return 0;
    }
    return std::max<std::int64_t>(0, mediaAt(now));
}

void ReplayClock::detach() noexcept {
    std::scoped_lock lock{mutex_};
    if (attached_ > 0) {
        --attached_;
    }
}

ReplayClock::Clock::time_point ReplayClock::dueTime(std::int64_t mediaNs) const {
    std::scoped_lock lock{mutex_};
    const double deltaNs = static_cast<double>(mediaNs - anchorMediaNs_) / speed_;
    return anchorWall_ + std::chrono::duration_cast<Clock::duration>(
                             std::chrono::duration<double, std::nano>{deltaNs});
}

void ReplayClock::setSpeed(double speed) {
    std::scoped_lock lock{mutex_};
    const auto now = Clock::now();
    anchorMediaNs_ = mediaAt(now); // re-anchor here: no jump in media time
    anchorWall_ = now;
    speed_ = speed;
}

double ReplayClock::speed() const {
    std::scoped_lock lock{mutex_};
    return speed_;
}

std::int64_t ReplayClock::mediaAt(Clock::time_point t) const {
    const double elapsedNs = std::chrono::duration<double, std::nano>{t - anchorWall_}.count();
    return anchorMediaNs_ + static_cast<std::int64_t>(elapsedNs * speed_);
}

} // namespace vsort::replay

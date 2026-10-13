#include "capture/capture_module.hpp"

#include <algorithm>
#include <format>
#include <system_error>
#include <utility>

#include <opencv2/imgcodecs.hpp>
#include <spdlog/spdlog.h>

#include <vsort/common/timestamp.hpp>

namespace vsort::service {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

constexpr std::uint32_t kMaxPhotos = 100000;
constexpr int kMaxSessionSuffix = 100;
constexpr std::uint64_t kBytesPerMb = 1024ULL * 1024ULL;

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

Result<fs::path> createSessionFolder(const fs::path& root) {
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        return makeError(Errc::IoError,
                         std::format("cannot create '{}': {}", toUtf8(root), ec.message()));
    }
    const std::string base = sessionName(WallTime::now());
    for (int attempt = 1; attempt <= kMaxSessionSuffix; ++attempt) {
        const fs::path dir = root / (attempt == 1 ? base : std::format("{}_{}", base, attempt));
        if (fs::create_directory(dir, ec)) {
            return dir;
        }
        if (ec) {
            return makeError(Errc::IoError,
                             std::format("cannot create '{}': {}", toUtf8(dir), ec.message()));
        }
    }
    return makeError(Errc::IoError, "no free session folder name");
}

std::string optionalNumber(const std::optional<double>& value) {
    return value ? std::format("{:.1f}", *value) : std::string{};
}

// <seq>_<verdict>_n<count>[_<length>mm]_f<frame id>; names sort by photo number.
std::string baseName(const CaptureImage& image) {
    std::string name =
        std::format("{:04}_{}_n{}", image.seq, verdictName(image.values.count), image.values.count);
    if (image.values.lengthMm) {
        name += std::format("_{:.0f}mm", *image.values.lengthMm);
    }
    name += std::format("_f{}", image.frameId);
    return name;
}

// Encodes in memory and writes with std::ofstream: works with UTF-8 paths on every platform.
Result<> writeJpeg(const fs::path& file, const cv::Mat& image, int quality) {
    std::vector<unsigned char> bytes;
    const std::vector<int> params{static_cast<int>(cv::IMWRITE_JPEG_QUALITY), quality};
    try {
        if (image.empty() || !cv::imencode(".jpg", image, bytes, params)) {
            return makeError(Errc::Internal, "cannot encode the photo");
        }
    } catch (const cv::Exception& e) {
        return makeError(Errc::Internal, std::string{"cannot encode the photo: "} + e.what());
    }
    std::ofstream out{file, std::ios::binary | std::ios::trunc};
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        return makeError(Errc::IoError, std::format("cannot write '{}'", toUtf8(file)));
    }
    return {};
}

} // namespace

std::vector<CaptureSensor> captureSensors(const MachineConfig& machine) {
    std::vector<CaptureSensor> out;
    for (const auto* lane : machine.lanes()) {
        for (const auto& sensor : lane->sensors) {
            out.push_back({.sensorId = sensor.id, .cameraId = sensor.cameraId});
        }
    }
    return out;
}

CaptureModule::CaptureModule(std::vector<CaptureSensor> sensors, CaptureOptions options)
    : options_{std::move(options)}
    , queue_{options_.queueDepth} {
    sensors_.reserve(sensors.size());
    for (const auto& sensor : sensors) {
        sensors_.push_back(SensorState{.sensor = sensor});
    }
}

CaptureModule::~CaptureModule() {
    stop();
}

Result<> CaptureModule::init(ModuleContext& /*context*/) {
    return {};
}

Result<> CaptureModule::start() {
    return {}; // a capture is started by the HMI (startCapture)
}

void CaptureModule::stop() noexcept {
    try {
        static_cast<void>(stopCapture());
    } catch (...) {
        // Nothing sensible left to do while shutting down.
    }
}

Health CaptureModule::health() const {
    const auto status = captureStatus();
    if (status.state == CaptureState::Failed) {
        return Health{.state = HealthState::Degraded,
                      .detail = "capture failed: " + status.errorText};
    }
    return Health{.state = HealthState::Ok,
                  .detail = status.state == CaptureState::Running ? "capturing" : "idle"};
}

CaptureStatus CaptureModule::statusLocked() const {
    CaptureStatus status;
    status.state = state_;
    status.sessionFolder = session_;
    status.defaultFolder = options_.defaultFolder;
    status.errorText = errorText_;
    for (const auto& s : sensors_) {
        status.sensors.push_back(CaptureSensorStatus{.sensorId = s.sensor.sensorId,
                                                     .cameraId = s.sensor.cameraId,
                                                     .selected = s.selected,
                                                     .saved = s.saved,
                                                     .target = s.selected ? target_ : 0U});
    }
    return status;
}

CaptureStatus CaptureModule::captureStatus() const {
    const std::scoped_lock lock{mutex_};
    return statusLocked();
}

CaptureModule::SensorState* CaptureModule::findLocked(std::uint16_t sensorId) {
    const auto it = std::ranges::find(sensors_, sensorId,
                                      [](const SensorState& s) { return s.sensor.sensorId; });
    return it == sensors_.end() ? nullptr : &*it;
}

Result<CaptureStatus> CaptureModule::startCapture(const CaptureRequest& request) {
    const std::scoped_lock control{control_};
    {
        const std::scoped_lock lock{mutex_};
        if (state_ == CaptureState::Running) {
            return makeError(Errc::AlreadyExists, "a capture is already running");
        }
        if (request.folder.empty() || !request.folder.is_absolute()) {
            return makeError(Errc::InvalidArgument, "the folder must be an absolute path");
        }
        if (request.photosPerSensor == 0 || request.photosPerSensor > kMaxPhotos) {
            return makeError(Errc::InvalidArgument,
                             std::format("photos per sensor must be 1..{}", kMaxPhotos));
        }
        if (request.sensorIds.empty()) {
            return makeError(Errc::InvalidArgument, "no sensor selected");
        }
        for (const auto id : request.sensorIds) {
            if (findLocked(id) == nullptr) {
                return makeError(Errc::InvalidArgument, std::format("unknown sensor {}", id));
            }
        }
    }
    if (auto valid = request.filter.validate(); !valid) {
        return std::unexpected{std::move(valid.error())};
    }

    stopWriter(); // a previous session: the writer finishes what is queued
    while (queue_.tryPop()) {
    }

    auto session = createSessionFolder(request.folder);
    if (!session) {
        return std::unexpected{std::move(session.error())};
    }
    std::error_code ec;
    const auto space = fs::space(request.folder, ec);
    if (!ec && space.available < options_.minFreeBytes) {
        fs::remove(*session, ec);
        return makeError(Errc::IoError,
                         std::format("less than {} MB free in '{}'",
                                     options_.minFreeBytes / kBytesPerMb, toUtf8(request.folder)));
    }
    csv_.close();
    csv_.clear();
    csv_.open(*session / "captures.csv", std::ios::trunc);
    csv_ << "sensor_id,camera_id,photo_no,frame_id,verdict,count,length_mm,width_mm,area_mm2,"
            "photo,overlay\n";
    csv_.flush();
    if (!csv_) {
        return makeError(Errc::IoError,
                         std::format("cannot write '{}'", toUtf8(*session / "captures.csv")));
    }

    {
        const std::scoped_lock lock{mutex_};
        state_ = CaptureState::Running;
        filter_ = request.filter;
        target_ = request.photosPerSensor;
        session_ = *session;
        errorText_.clear();
        for (auto& s : sensors_) {
            s.selected =
                std::ranges::find(request.sensorIds, s.sensor.sensorId) != request.sensorIds.end();
            s.reserved = 0;
            s.saved = 0;
        }
        active_.store(true, std::memory_order_release);
    }
    writer_ = std::jthread{[this](const std::stop_token& stop) { runWriter(stop); }};
    spdlog::info("capture: {} photo(s) for {} sensor(s) to {}", request.photosPerSensor,
                 request.sensorIds.size(), toUtf8(*session));
    return captureStatus();
}

CaptureStatus CaptureModule::stopCapture() {
    const std::scoped_lock control{control_};
    {
        const std::scoped_lock lock{mutex_};
        if (state_ == CaptureState::Running) {
            state_ = CaptureState::Stopped;
            active_.store(false, std::memory_order_release);
        }
    }
    stopWriter();
    return captureStatus();
}

void CaptureModule::stopWriter() noexcept {
    if (writer_.joinable()) {
        writer_.request_stop(); // the writer first empties its queue
        writer_.join();
    }
}

std::optional<std::uint32_t> CaptureModule::reserve(std::uint16_t sensorId,
                                                    const CaptureValues& values) noexcept {
    if (!active_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    try {
        const std::scoped_lock lock{mutex_};
        if (state_ != CaptureState::Running) {
            return std::nullopt;
        }
        auto* sensor = findLocked(sensorId);
        if (sensor == nullptr || !sensor->selected || sensor->reserved >= target_ ||
            !filter_.accepts(values)) {
            return std::nullopt;
        }
        return ++sensor->reserved;
    } catch (...) {
        return std::nullopt;
    }
}

void CaptureModule::cancel(std::uint16_t sensorId) noexcept {
    try {
        const std::scoped_lock lock{mutex_};
        if (auto* sensor = findLocked(sensorId); sensor != nullptr && sensor->reserved > 0) {
            --sensor->reserved;
        }
    } catch (...) {
        // The photo number stays used: one photo fewer than the quota, no harm.
    }
}

void CaptureModule::commit(CaptureImage image) noexcept {
    const auto sensorId = image.sensorId;
    try {
        if (queue_.tryPush(std::move(image))) {
            return;
        }
    } catch (...) {
        // Falls through: handled like a full queue.
    }
    spdlog::warn("capture: writer queue full, photo of sensor {} skipped", sensorId);
    cancel(sensorId);
}

void CaptureModule::runWriter(const std::stop_token& stop) {
    for (;;) {
        auto image = queue_.tryPop();
        if (!image) {
            if (stop.stop_requested()) {
                return;
            }
            std::this_thread::sleep_for(1ms);
            continue;
        }
        try {
            write(*image);
        } catch (const std::exception& e) {
            fail(e.what());
        }
    }
}

void CaptureModule::write(const CaptureImage& image) {
    fs::path session;
    {
        const std::scoped_lock lock{mutex_};
        if (state_ == CaptureState::Failed) {
            return; // the capture already failed: drop what is left in the queue
        }
        session = session_;
    }
    const fs::path dir = session / std::format("sensor_{}", image.sensorId);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        fail(std::format("cannot create '{}': {}", toUtf8(dir), ec.message()));
        return;
    }
    const std::string base = baseName(image);
    const std::string photoFile = base + ".jpg";
    const std::string overlayFile = base + "_overlay.jpg";
    for (const auto& [file, picture] :
         {std::pair{photoFile, &image.photo}, std::pair{overlayFile, &image.overlay}}) {
        if (auto written = writeJpeg(dir / file, *picture, options_.jpegQuality); !written) {
            fail(written.error().message);
            return;
        }
    }
    const auto& v = image.values;
    csv_ << std::format("{},{},{},{},{},{},{},{},{},sensor_{}/{},sensor_{}/{}\n", image.sensorId,
                        image.cameraId, image.seq, image.frameId, verdictName(v.count), v.count,
                        optionalNumber(v.lengthMm), optionalNumber(v.widthMm),
                        optionalNumber(v.areaMm2), image.sensorId, photoFile, image.sensorId,
                        overlayFile);
    csv_.flush();

    const std::scoped_lock lock{mutex_};
    if (auto* sensor = findLocked(image.sensorId); sensor != nullptr) {
        ++sensor->saved;
    }
    const bool allDone = std::ranges::all_of(
        sensors_, [this](const SensorState& s) { return !s.selected || s.saved >= target_; });
    if (state_ == CaptureState::Running && allDone) {
        state_ = CaptureState::Done;
        active_.store(false, std::memory_order_release);
        spdlog::info("capture: done, photos are in {}", toUtf8(session_));
    }
}

void CaptureModule::fail(const std::string& what) {
    const std::scoped_lock lock{mutex_};
    if (state_ != CaptureState::Running) {
        return;
    }
    state_ = CaptureState::Failed;
    errorText_ = what;
    active_.store(false, std::memory_order_release);
    spdlog::error("capture failed: {}", what);
}

} // namespace vsort::service

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <vsort/camera/daheng.hpp>
#include <vsort/camera/frame_pool.hpp>
#include <vsort/camera/genicam.hpp>
#include <vsort/common/logging.hpp>
#include <vsort/common/timestamp.hpp>

#include "galaxy_lib.hpp"

namespace vsort::camera {

namespace {

using detail::fail;

// Daheng manual: the timestamp unit of the USB3 cameras is ns.
constexpr std::uint64_t kUsb3TickHz = 1'000'000'000;

// --- GenICam feature access -------------------------------------------------------------

Result<> setEnum(GX_DEV_HANDLE h, const char* name, std::string_view value) {
    const std::string text{value};
    if (const GX_STATUS st = GXSetEnumValueByString(h, name, text.c_str());
        st != GX_STATUS_SUCCESS) {
        return fail(st, std::string{"set "} + name + "=" + text);
    }
    return {};
}

Result<> setFloat(GX_DEV_HANDLE h, const char* name, double value) {
    if (const GX_STATUS st = GXSetFloatValue(h, name, value); st != GX_STATUS_SUCCESS) {
        return fail(st, std::string{"set "} + name);
    }
    return {};
}

Result<> setInt(GX_DEV_HANDLE h, const char* name, std::int64_t value) {
    if (const GX_STATUS st = GXSetIntValue(h, name, value); st != GX_STATUS_SUCCESS) {
        return fail(st, std::string{"set "} + name);
    }
    return {};
}

Result<GX_INT_VALUE> getInt(GX_DEV_HANDLE h, const char* name) {
    GX_INT_VALUE value{};
    if (const GX_STATUS st = GXGetIntValue(h, name, &value); st != GX_STATUS_SUCCESS) {
        return fail(st, std::string{"get "} + name);
    }
    return value;
}

Result<GX_FLOAT_VALUE> getFloat(GX_DEV_HANDLE h, const char* name) {
    GX_FLOAT_VALUE value{};
    if (const GX_STATUS st = GXGetFloatValue(h, name, &value); st != GX_STATUS_SUCCESS) {
        return fail(st, std::string{"get "} + name);
    }
    return value;
}

Result<std::string> getSymbol(GX_DEV_HANDLE h, const char* name) {
    auto value = std::make_unique<GX_ENUM_VALUE>(); // ~20 kB, keep it off the stack
    if (const GX_STATUS st = GXGetEnumValue(h, name, value.get()); st != GX_STATUS_SUCCESS) {
        return fail(st, std::string{"get "} + name);
    }
    return detail::fromChars(value->stCurValue.strCurSymbolic);
}

Result<std::string> getString(GX_DEV_HANDLE h, const char* name) {
    GX_STRING_VALUE value{};
    if (const GX_STATUS st = GXGetStringValue(h, name, &value); st != GX_STATUS_SUCCESS) {
        return fail(st, std::string{"get "} + name);
    }
    return detail::fromChars(value.strCurValue);
}

template <typename T>
[[nodiscard]] std::unexpected<Error> propagate(const Result<T>& result) {
    return std::unexpected<Error>{result.error()};
}

[[nodiscard]] std::uint32_t toU32(std::int64_t value) noexcept {
    return value < 0 ? 0U : static_cast<std::uint32_t>(std::min<std::int64_t>(value, 0xFFFFFFFF));
}

[[nodiscard]] std::optional<PixelFormat> toPixelFormat(std::int32_t format) noexcept {
    switch (format) {
    case GX_PIXEL_FORMAT_MONO8:
        return PixelFormat::Mono8;
    case GX_PIXEL_FORMAT_BAYER_RG8:
        return PixelFormat::BayerRG8;
    case GX_PIXEL_FORMAT_RGB8:
        return PixelFormat::Rgb8;
    case GX_PIXEL_FORMAT_BGR8:
        return PixelFormat::Bgr8;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool sameRoi(const Roi& a, const Roi& b) noexcept {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

// pvoid64 is a pointer or a 64-bit integer depending on the SDK build.
template <typename P>
[[nodiscard]] void* toPointer(P value) noexcept {
    return reinterpret_cast<void*>(std::bit_cast<std::uintptr_t>(value));
}

// --- Camera -----------------------------------------------------------------------------

class DahengCamera final : public ICamera {
public:
    explicit DahengCamera(const DahengOptions& options)
        : options_{options} {}
    ~DahengCamera() override { close(); }

    DahengCamera(const DahengCamera&) = delete;
    DahengCamera& operator=(const DahengCamera&) = delete;
    DahengCamera(DahengCamera&&) = delete;
    DahengCamera& operator=(DahengCamera&&) = delete;

    [[nodiscard]] Result<> open(std::string_view serial) override {
        if (serial.empty()) {
            return makeError(Errc::InvalidArgument, "empty serial");
        }
        if (open_) {
            return makeError(Errc::AlreadyExists, "camera already open");
        }
        auto lib = detail::GalaxyLib::acquire();
        if (!lib) {
            return propagate(lib);
        }

        std::string sn{serial};
        GX_DEV_HANDLE handle{};
        bool usb3 = false;
        {
            const auto lock = detail::lockDeviceList();
            const auto count = detail::updateDeviceList();
            if (!count) {
                return propagate(count);
            }
            const auto deviceClass = detail::deviceClassOf(sn, *count);
            usb3 = deviceClass.has_value() && *deviceClass == GX_DEVICE_CLASS_U3V;
            GX_OPEN_PARAM param{};
            param.pszContent = sn.data();
            param.openMode = GX_OPEN_SN;
            param.accessMode = GX_ACCESS_EXCLUSIVE;
            if (const GX_STATUS st = GXOpenDevice(&param, &handle); st != GX_STATUS_SUCCESS) {
                return fail(st, "GXOpenDevice(" + sn + ")");
            }
        }

        lib_ = std::move(*lib);
        handle_ = handle;
        usb3_ = usb3;
        open_ = true;
        if (auto read = readInfo(sn); !read) {
            const Error error = read.error();
            close();
            return std::unexpected<Error>{error};
        }
        return {};
    }

    void close() noexcept override {
        stop();
        if (open_) {
            if (const GX_STATUS st = GXCloseDevice(handle_); st != GX_STATUS_SUCCESS) {
                warn("GXCloseDevice failed, SDK status " + std::to_string(st));
            }
            open_ = false;
            handle_ = {};
        }
        lib_.reset(); // after GXCloseDevice
    }

    [[nodiscard]] bool isOpen() const noexcept override { return open_; }

    [[nodiscard]] Result<CameraInfo> info() const override {
        if (!open_) {
            return makeError(Errc::NotFound, "not open");
        }
        return info_;
    }

    [[nodiscard]] Result<CameraSettings> settings() const override {
        if (!open_) {
            return makeError(Errc::NotFound, "not open");
        }
        const auto exposure = getFloat(handle_, "ExposureTime");
        const auto gain = getFloat(handle_, "Gain");
        const auto offsetX = getInt(handle_, "OffsetX");
        const auto offsetY = getInt(handle_, "OffsetY");
        const auto width = getInt(handle_, "Width");
        const auto height = getInt(handle_, "Height");
        const auto mode = getSymbol(handle_, "TriggerMode");
        if (!exposure) {
            return propagate(exposure);
        }
        if (!gain) {
            return propagate(gain);
        }
        if (!offsetX) {
            return propagate(offsetX);
        }
        if (!offsetY) {
            return propagate(offsetY);
        }
        if (!width) {
            return propagate(width);
        }
        if (!height) {
            return propagate(height);
        }
        if (!mode) {
            return propagate(mode);
        }

        CameraSettings s;
        s.exposureUs = exposure->dCurValue;
        s.gainDb = gain->dCurValue;
        const Roi roi{.x = toU32(offsetX->nCurValue),
                      .y = toU32(offsetY->nCurValue),
                      .width = toU32(width->nCurValue),
                      .height = toU32(height->nCurValue)};
        const bool fullSensor = roi.x == 0 && roi.y == 0 && roi.width == info_.sensorWidth &&
                                roi.height == info_.sensorHeight;
        s.roi = fullSensor ? Roi{} : roi;

        if (*mode == "Off") {
            s.triggerMode = TriggerMode::FreeRun;
            return s;
        }
        const auto source = getSymbol(handle_, "TriggerSource");
        if (!source) {
            return propagate(source);
        }
        if (*source == triggerPlan(TriggerMode::Software, TriggerEdge::Rising).source) {
            s.triggerMode = TriggerMode::Software;
        } else if (*source == triggerPlan(TriggerMode::Hardware, TriggerEdge::Rising).source) {
            s.triggerMode = TriggerMode::Hardware;
            const auto activation = getSymbol(handle_, "TriggerActivation");
            if (!activation) {
                return propagate(activation);
            }
            s.triggerEdge =
                *activation == triggerPlan(TriggerMode::Hardware, TriggerEdge::Falling).activation
                    ? TriggerEdge::Falling
                    : TriggerEdge::Rising;
        } else {
            return makeError(Errc::NotSupported, "unsupported trigger source " + *source);
        }
        return s;
    }

    // Exposure and gain can change while streaming. ROI and trigger changes are only sent to
    // the camera when they differ from its current state; the camera rejects them while streaming.
    [[nodiscard]] Result<> applySettings(const CameraSettings& wanted) override {
        if (!open_) {
            return makeError(Errc::NotFound, "not open");
        }
        if (auto valid = validate(wanted, info_); !valid) {
            return valid;
        }

        const auto current = settings();
        const bool known = current.has_value();

        if (!known || current->triggerMode != wanted.triggerMode ||
            (wanted.triggerMode == TriggerMode::Hardware &&
             current->triggerEdge != wanted.triggerEdge)) {
            if (auto r = applyTrigger(wanted); !r) {
                return r;
            }
        }
        if (!known || current->exposureUs != wanted.exposureUs) {
            if (auto r = setFloat(handle_, "ExposureTime", wanted.exposureUs); !r) {
                return r;
            }
        }
        if (!known || current->gainDb != wanted.gainDb) {
            if (auto r = setFloat(handle_, "Gain", wanted.gainDb); !r) {
                return r;
            }
        }
        if (!known || !sameRoi(current->roi, wanted.roi)) {
            if (auto r = applyRoi(wanted.roi); !r) {
                return r;
            }
        }
        return {};
    }

    // Not safe while streaming (the grab thread reads it).
    void setFrameCallback(FrameCallback callback) override { callback_ = std::move(callback); }

    [[nodiscard]] Result<> start() override {
        if (!open_) {
            return makeError(Errc::NotFound, "not open");
        }
        if (streaming_) {
            return {};
        }

        const auto payload = getInt(handle_, "PayloadSize");
        if (!payload) {
            return propagate(payload);
        }
        auto pool =
            FramePool::create(options_.poolFrames, static_cast<std::size_t>(payload->nCurValue));
        if (!pool) {
            return propagate(pool);
        }
        if (auto r = setEnum(handle_, "AcquisitionMode", "Continuous"); !r) {
            return r;
        }

        pool_ = std::move(*pool);
        dropped_ = 0;
        if (const GX_STATUS st = GXRegisterCaptureCallbackEx(handle_, this, &onFrame);
            st != GX_STATUS_SUCCESS) {
            pool_.reset();
            return fail(st, "GXRegisterCaptureCallbackEx");
        }
        streaming_ = true; // before StreamOn so the first frame is not discarded
        if (const GX_STATUS st = GXStreamOn(handle_); st != GX_STATUS_SUCCESS) {
            streaming_ = false;
            static_cast<void>(GXUnregisterCaptureCallback(handle_));
            pool_.reset();
            return fail(st, "GXStreamOn");
        }
        return {};
    }

    void stop() noexcept override {
        if (!streaming_.exchange(false)) {
            return;
        }
        if (const GX_STATUS st = GXStreamOff(handle_); st != GX_STATUS_SUCCESS) {
            warn("GXStreamOff failed, SDK status " + std::to_string(st));
        }
        static_cast<void>(GXUnregisterCaptureCallback(handle_));

        // Contract: no callback after stop() returns. Wait for one that is still running.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (inFlight_.load() > 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        pool_.reset();
    }

    [[nodiscard]] bool isStreaming() const noexcept override { return streaming_; }

private:
    [[nodiscard]] static Result<> validate(const CameraSettings& s, const CameraInfo& info) {
        if (!std::isfinite(s.exposureUs) || s.exposureUs <= 0.0) {
            return makeError(Errc::InvalidArgument, "exposure must be > 0");
        }
        if (!std::isfinite(s.gainDb) || s.gainDb < 0.0) {
            return makeError(Errc::InvalidArgument, "gain must be >= 0");
        }
        const Roi& r = s.roi;
        if ((r.width == 0) != (r.height == 0)) {
            return makeError(Errc::InvalidArgument,
                             "ROI width and height must both be 0 or both set");
        }
        if (r.width != 0) {
            const std::uint64_t right = std::uint64_t{r.x} + r.width;
            const std::uint64_t bottom = std::uint64_t{r.y} + r.height;
            if (right > info.sensorWidth || bottom > info.sensorHeight) {
                return makeError(Errc::InvalidArgument, "ROI outside the sensor");
            }
        }
        return {};
    }

    [[nodiscard]] Result<> readInfo(const std::string& serial) {
        const auto width = getInt(handle_, "WidthMax");
        const auto height = getInt(handle_, "HeightMax");
        if (!width) {
            return propagate(width);
        }
        if (!height) {
            return propagate(height);
        }
        info_ = CameraInfo{.serial = serial,
                           .model = getString(handle_, "DeviceModelName").value_or(""),
                           .sensorWidth = toU32(width->nCurValue),
                           .sensorHeight = toU32(height->nCurValue)};

        // Device timestamp unit, from the official source; never derived from measurements.
        // Without one, deviceTimestampNs stays 0 (see FrameMetadata).
        std::string source = "none, device timestamps disabled";
        tickHz_ = 0;
        if (const auto hz = getInt(handle_, "TimestampTickFrequency"); hz && hz->nCurValue > 0) {
            tickHz_ = static_cast<std::uint64_t>(hz->nCurValue);
            source = "TimestampTickFrequency";
        } else if (const auto gev = getInt(handle_, "GevTimestampTickFrequency");
                   gev && gev->nCurValue > 0) {
            tickHz_ = static_cast<std::uint64_t>(gev->nCurValue);
            source = "GevTimestampTickFrequency";
        } else if (usb3_) {
            tickHz_ = kUsb3TickHz;
            source = "USB3 camera, Daheng manual: timestamp unit is ns";
        }
        note(serial + ": timestamp tick frequency " + std::to_string(tickHz_) + " Hz (" + source +
             ")");
        return {};
    }

    [[nodiscard]] Result<> applyTrigger(const CameraSettings& s) {
        const TriggerPlan plan = triggerPlan(s.triggerMode, s.triggerEdge);
        // Not every model has a TriggerSelector; FrameStart is the default where it exists.
        static_cast<void>(setEnum(handle_, "TriggerSelector", "FrameStart"));
        if (auto r = setEnum(handle_, "TriggerMode", plan.triggerOn ? "On" : "Off"); !r) {
            return r;
        }
        if (!plan.source.empty()) {
            if (auto r = setEnum(handle_, "TriggerSource", plan.source); !r) {
                return r;
            }
        }
        if (!plan.activation.empty()) {
            if (auto r = setEnum(handle_, "TriggerActivation", plan.activation); !r) {
                return r;
            }
        }
        return {};
    }

    // Offsets first to 0 so the new size always fits, then size, then the real offsets.
    [[nodiscard]] Result<> applyRoi(const Roi& roi) {
        const bool full = roi.width == 0;
        const std::int64_t width = full ? info_.sensorWidth : roi.width;
        const std::int64_t height = full ? info_.sensorHeight : roi.height;
        const std::int64_t x = full ? 0 : roi.x;
        const std::int64_t y = full ? 0 : roi.y;
        for (const auto& [name, value] : {std::pair<const char*, std::int64_t>{"OffsetX", 0},
                                          {"OffsetY", 0},
                                          {"Width", width},
                                          {"Height", height},
                                          {"OffsetX", x},
                                          {"OffsetY", y}}) {
            if (auto r = setInt(handle_, name, value); !r) {
                return r;
            }
        }
        return {};
    }

    // Rate limited: first drop and then every 1000th.
    void noteDrop(const std::string& reason) noexcept {
        const std::uint64_t n = dropped_.fetch_add(1) + 1;
        if (n == 1 || n % 1000 == 0) {
            warn(info_.serial + ": frame dropped (" + reason + "), drops so far " +
                 std::to_string(n));
        }
    }

    static void warn(const std::string& message) noexcept {
        try {
            log::get("camera")->warn("{}", message);
        } catch (...) {
            // Logging must never take the grab thread down.
        }
    }

    static void note(const std::string& message) noexcept {
        try {
            log::get("camera")->info("{}", message);
        } catch (...) {
            // Logging must never take the grab thread down.
        }
    }

    // Runs on the SDK's grab thread.
    void handleFrame(const GX_FRAME_DATA_EX& f) noexcept {
        const Timestamp arrival = Timestamp::now();
        inFlight_.fetch_add(1);
        const struct Release {
            std::atomic<int>& counter;
            ~Release() { counter.fetch_sub(1); }
        } release{inFlight_};
        if (!streaming_) {
            return;
        }

        try {
            if (f.nStatus != GX_FRAME_STATUS_SUCCESS) {
                noteDrop("incomplete frame");
                return;
            }
            const auto format = toPixelFormat(f.nPixelFormat);
            if (!format) {
                noteDrop("unsupported pixel format " + std::to_string(f.nPixelFormat));
                return;
            }
            if (f.nWidth <= 0 || f.nHeight <= 0 || f.nImgSize <= 0 ||
                toPointer(f.pImgBuf) == nullptr) {
                noteDrop("invalid frame data");
                return;
            }
            const auto size = static_cast<std::size_t>(f.nImgSize);
            const auto buffer = pool_->tryAcquire();
            if (!buffer) {
                noteDrop("buffer pool exhausted");
                return;
            }
            if (size > buffer->data.size()) {
                noteDrop("frame larger than payload size");
                return;
            }
            const auto* source = static_cast<const std::byte*>(toPointer(f.pImgBuf));
            std::copy_n(source, size, buffer->data.data());

            const Frame frame{
                .meta = FrameMetadata{.frameId = FrameId{f.nFrameID},
                                      .hostTimestamp = arrival,
                                      .deviceTimestampNs = ticksToNs(f.nTimestamp, tickHz_),
                                      .width = static_cast<std::uint32_t>(f.nWidth),
                                      .height = static_cast<std::uint32_t>(f.nHeight),
                                      .strideBytes = static_cast<std::uint32_t>(
                                          size / static_cast<std::size_t>(f.nHeight)),
                                      .pixelFormat = *format,
                                      .cameraIndex = 0},
                .data = std::span<const std::byte>{buffer->data.data(), size},
                .owner = buffer};
            if (callback_) {
                callback_(frame);
            }
        } catch (...) {
            noteDrop("exception in frame callback");
        }
    }

    static void GX_STDC onFrame(GX_FRAME_DATA_EX* frame) {
        if (frame == nullptr) {
            return;
        }
        auto* self = static_cast<DahengCamera*>(toPointer(frame->pUserParam));
        if (self != nullptr) {
            self->handleFrame(*frame);
        }
    }

    DahengOptions options_;
    std::shared_ptr<detail::GalaxyLib> lib_;
    GX_DEV_HANDLE handle_{};
    bool open_{false};
    bool usb3_{false};
    CameraInfo info_;
    std::uint64_t tickHz_{0};
    FrameCallback callback_;
    std::shared_ptr<FramePool> pool_;
    std::atomic<bool> streaming_{false};
    std::atomic<int> inFlight_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

} // namespace

std::unique_ptr<ICamera> makeDahengCamera(const DahengOptions& options) {
    return std::make_unique<DahengCamera>(options);
}

} // namespace vsort::camera

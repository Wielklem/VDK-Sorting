#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <vsort/common/error.hpp>
#include <vsort/platform/shared_memory.hpp>

namespace vsort::ipc {

// Preview frame ring in shared memory (docs/ipc-design.md section 5).
// One writer (service), any number of readers (HMI, tools). Lock-free, lossy, newest frame wins.

inline constexpr std::uint16_t kDefaultSlotCount = 4;

enum class PreviewPixelFormat : std::uint32_t { Mono8 = 1, Rgb8 = 2 };

[[nodiscard]] constexpr std::uint32_t bytesPerPixel(PreviewPixelFormat format) noexcept {
    return format == PreviewPixelFormat::Rgb8 ? 3U : 1U;
}

// "vdk_preview_<cameraId>_<generation>". The generation is part of the name, so an old ring can
// be closed (which removes its name) without touching its successor.
[[nodiscard]] std::string previewRingName(std::uint16_t cameraId, std::uint32_t generation);

struct RingSpec {
    std::uint16_t slotCount{kDefaultSlotCount};
    std::uint32_t slotSize{0}; // max payload bytes per frame
    std::uint32_t generation{1};
};

struct PreviewFrameInfo {
    std::uint64_t frameId{0};
    std::uint64_t timestampNs{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t strideBytes{0};
    PreviewPixelFormat pixelFormat{PreviewPixelFormat::Mono8};
};

struct PreviewFrame {
    PreviewFrameInfo info;
    std::vector<std::byte> pixels;
};

class PreviewRingWriter {
public:
    // InvalidArgument: bad name, zero slotSize/slotCount. IoError: shared memory failed.
    [[nodiscard]] static Result<std::unique_ptr<PreviewRingWriter>> create(std::string_view name,
                                                                           const RingSpec& spec);
    ~PreviewRingWriter();
    PreviewRingWriter(const PreviewRingWriter&) = delete;
    PreviewRingWriter& operator=(const PreviewRingWriter&) = delete;
    PreviewRingWriter(PreviewRingWriter&&) = delete;
    PreviewRingWriter& operator=(PreviewRingWriter&&) = delete;

    // Single writer thread only. InvalidArgument when pixels.size() > slotSize.
    [[nodiscard]] Result<> write(const PreviewFrameInfo& info, std::span<const std::byte> pixels);

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] std::uint32_t generation() const noexcept { return spec_.generation; }
    [[nodiscard]] std::uint32_t slotSize() const noexcept { return spec_.slotSize; }
    [[nodiscard]] std::uint64_t framesWritten() const noexcept { return framesWritten_; }

private:
    PreviewRingWriter(std::unique_ptr<platform::ISharedMemory> memory, const RingSpec& spec);

    std::unique_ptr<platform::ISharedMemory> memory_;
    RingSpec spec_;
    std::uint64_t framesWritten_{0};
};

class PreviewRingReader {
public:
    // NotFound: no such ring. ValidationFailed: wrong magic, layout version or size.
    [[nodiscard]] static Result<std::unique_ptr<PreviewRingReader>> open(std::string_view name);
    ~PreviewRingReader();
    PreviewRingReader(const PreviewRingReader&) = delete;
    PreviewRingReader& operator=(const PreviewRingReader&) = delete;
    PreviewRingReader(PreviewRingReader&&) = delete;
    PreviewRingReader& operator=(PreviewRingReader&&) = delete;

    // Returns the newest committed frame, or nullopt when nothing new was written since
    // `lastSeen` (start with 0; updated on success). Never blocks the writer.
    [[nodiscard]] std::optional<PreviewFrame> readNewest(std::uint64_t& lastSeen) const;

    [[nodiscard]] std::uint32_t generation() const noexcept { return generation_; }
    [[nodiscard]] std::uint32_t slotSize() const noexcept { return slotSize_; }

private:
    PreviewRingReader(std::unique_ptr<platform::ISharedMemory> memory, std::uint16_t slotCount,
                      std::uint32_t slotSize, std::uint32_t generation);

    std::unique_ptr<platform::ISharedMemory> memory_;
    std::uint16_t slotCount_;
    std::uint32_t slotSize_;
    std::uint32_t generation_;
};

} // namespace vsort::ipc

#include <atomic>
#include <cstring>
#include <string>

#include <vsort/ipc/preview_ring.hpp>

namespace vsort::ipc {
namespace {

constexpr std::uint32_t kMagic = 0x56444B50; // "VDKP"
constexpr std::uint16_t kLayoutVersion = 1;
constexpr std::size_t kAlign = 64;
constexpr std::uint64_t kMaxRingBytes = std::uint64_t{1} << 30; // sanity limit: 1 GiB

// Fixed-size integers only, little-endian hosts only (x64). Both structs are 64 bytes.
struct RingHeader {
    std::uint32_t magic;
    std::uint16_t layoutVersion;
    std::uint16_t slotCount;
    std::uint32_t slotSize;
    std::uint32_t generation;
    std::uint64_t writeSeq; // number of committed frames; accessed atomically
    std::uint8_t reserved[40];
};

struct SlotHeader {
    std::uint64_t seq; // even = stable, odd = being written; +2 per frame; accessed atomically
    std::uint64_t frameId;
    std::uint64_t timestampNs;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t strideBytes;
    std::uint32_t pixelFormat;
    std::uint32_t payloadSize;
    std::uint8_t reserved[20];
};

static_assert(sizeof(RingHeader) == kAlign);
static_assert(sizeof(SlotHeader) == kAlign);
static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic_ref<std::uint64_t>::required_alignment <= 8);

constexpr std::uint64_t roundUp(std::uint64_t value) noexcept {
    return (value + kAlign - 1) / kAlign * kAlign;
}
constexpr std::uint64_t slotStride(std::uint32_t slotSize) noexcept {
    return sizeof(SlotHeader) + roundUp(slotSize);
}
constexpr std::uint64_t totalSize(std::uint16_t slotCount, std::uint32_t slotSize) noexcept {
    return sizeof(RingHeader) + static_cast<std::uint64_t>(slotCount) * slotStride(slotSize);
}

std::byte* slotPtr(std::byte* base, std::uint16_t slotCount, std::uint32_t slotSize,
                   std::uint64_t index) noexcept {
    return base + sizeof(RingHeader) + (index % slotCount) * slotStride(slotSize);
}

} // namespace

std::string previewRingName(std::uint16_t cameraId, std::uint32_t generation) {
    return "vdk_preview_" + std::to_string(cameraId) + "_" + std::to_string(generation);
}

// ---------------------------------------------------------------- writer

PreviewRingWriter::PreviewRingWriter(std::unique_ptr<platform::ISharedMemory> memory,
                                     const RingSpec& spec)
    : memory_{std::move(memory)}
    , spec_{spec} {}

PreviewRingWriter::~PreviewRingWriter() = default;

const std::string& PreviewRingWriter::name() const noexcept {
    return memory_->name();
}

Result<std::unique_ptr<PreviewRingWriter>> PreviewRingWriter::create(std::string_view name,
                                                                     const RingSpec& spec) {
    if (spec.slotCount == 0 || spec.slotSize == 0) {
        return makeError(Errc::InvalidArgument, "ring needs slotCount > 0 and slotSize > 0");
    }
    const std::uint64_t size = totalSize(spec.slotCount, spec.slotSize);
    if (size > kMaxRingBytes) {
        return makeError(Errc::InvalidArgument, "ring too large");
    }
    auto memory = platform::createSharedMemory(name, static_cast<std::size_t>(size));
    if (!memory) {
        return std::unexpected{memory.error()};
    }
    // Fresh shared memory is zero-filled (writeSeq 0, all slot seq 0).
    auto* header = reinterpret_cast<RingHeader*>((*memory)->bytes().data());
    header->magic = kMagic;
    header->layoutVersion = kLayoutVersion;
    header->slotCount = spec.slotCount;
    header->slotSize = spec.slotSize;
    header->generation = spec.generation;
    return std::unique_ptr<PreviewRingWriter>{new PreviewRingWriter{std::move(*memory), spec}};
}

Result<> PreviewRingWriter::write(const PreviewFrameInfo& info, std::span<const std::byte> pixels) {
    if (pixels.size() > spec_.slotSize) {
        return makeError(Errc::InvalidArgument, "frame larger than ring slot");
    }
    std::byte* base = memory_->bytes().data();
    auto* header = reinterpret_cast<RingHeader*>(base);
    std::atomic_ref<std::uint64_t> writeSeq{header->writeSeq};

    const std::uint64_t index = writeSeq.load(std::memory_order_relaxed);
    std::byte* slotBytes = slotPtr(base, spec_.slotCount, spec_.slotSize, index);
    auto* slot = reinterpret_cast<SlotHeader*>(slotBytes);
    std::atomic_ref<std::uint64_t> seq{slot->seq};

    const std::uint64_t before = seq.load(std::memory_order_relaxed);
    seq.store(before + 1, std::memory_order_relaxed); // odd: readers must not trust the slot
    std::atomic_thread_fence(std::memory_order_release);

    slot->frameId = info.frameId;
    slot->timestampNs = info.timestampNs;
    slot->width = info.width;
    slot->height = info.height;
    slot->strideBytes = info.strideBytes;
    slot->pixelFormat = static_cast<std::uint32_t>(info.pixelFormat);
    slot->payloadSize = static_cast<std::uint32_t>(pixels.size());
    if (!pixels.empty()) {
        std::memcpy(slotBytes + sizeof(SlotHeader), pixels.data(), pixels.size());
    }

    seq.store(before + 2, std::memory_order_release); // even: slot is consistent again
    writeSeq.store(index + 1, std::memory_order_release);
    ++framesWritten_;
    return {};
}

// ---------------------------------------------------------------- reader

PreviewRingReader::PreviewRingReader(std::unique_ptr<platform::ISharedMemory> memory,
                                     std::uint16_t slotCount, std::uint32_t slotSize,
                                     std::uint32_t generation)
    : memory_{std::move(memory)}
    , slotCount_{slotCount}
    , slotSize_{slotSize}
    , generation_{generation} {}

PreviewRingReader::~PreviewRingReader() = default;

Result<std::unique_ptr<PreviewRingReader>> PreviewRingReader::open(std::string_view name) {
    auto memory = platform::openSharedMemory(name);
    if (!memory) {
        return std::unexpected{memory.error()};
    }
    if ((*memory)->size() < sizeof(RingHeader)) {
        return makeError(Errc::ValidationFailed, "ring too small");
    }
    RingHeader header{};
    std::memcpy(&header, (*memory)->bytes().data(), sizeof(header));
    if (header.magic != kMagic || header.layoutVersion != kLayoutVersion) {
        return makeError(Errc::ValidationFailed, "not a preview ring or unsupported layout");
    }
    if (header.slotCount == 0 || header.slotSize == 0 ||
        (*memory)->size() < totalSize(header.slotCount, header.slotSize)) {
        return makeError(Errc::ValidationFailed, "ring header inconsistent with its size");
    }
    return std::unique_ptr<PreviewRingReader>{new PreviewRingReader{
        std::move(*memory), header.slotCount, header.slotSize, header.generation}};
}

std::optional<PreviewFrame> PreviewRingReader::readNewest(std::uint64_t& lastSeen) const {
    std::byte* base = memory_->bytes().data();
    auto* header = reinterpret_cast<RingHeader*>(base);
    const std::atomic_ref<std::uint64_t> writeSeq{header->writeSeq};

    constexpr int kAttempts = 4;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        const std::uint64_t committed = writeSeq.load(std::memory_order_acquire);
        if (committed == 0 || committed == lastSeen) {
            return std::nullopt;
        }
        std::byte* slotBytes = slotPtr(base, slotCount_, slotSize_, committed - 1);
        auto* slot = reinterpret_cast<SlotHeader*>(slotBytes);
        const std::atomic_ref<std::uint64_t> seq{slot->seq};

        const std::uint64_t first = seq.load(std::memory_order_acquire);
        if ((first & 1U) != 0 || first == 0) {
            continue; // being written
        }
        SlotHeader copy{};
        std::memcpy(&copy, slotBytes, sizeof(copy));
        PreviewFrame frame;
        const bool sizeOk = copy.payloadSize <= slotSize_;
        if (sizeOk) {
            frame.pixels.resize(copy.payloadSize);
            std::memcpy(frame.pixels.data(), slotBytes + sizeof(SlotHeader), copy.payloadSize);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq.load(std::memory_order_relaxed) != first) {
            continue; // overwritten while copying: try again with the newest slot
        }
        if (!sizeOk) {
            return std::nullopt;
        }
        frame.info =
            PreviewFrameInfo{.frameId = copy.frameId,
                             .timestampNs = copy.timestampNs,
                             .width = copy.width,
                             .height = copy.height,
                             .strideBytes = copy.strideBytes,
                             .pixelFormat = static_cast<PreviewPixelFormat>(copy.pixelFormat)};
        lastSeen = committed;
        return frame;
    }
    return std::nullopt;
}

} // namespace vsort::ipc

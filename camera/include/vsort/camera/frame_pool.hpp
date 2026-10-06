#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <vector>

#include <vsort/common/error.hpp>

namespace vsort::camera {

// One slot of the pool. `data` is writable pixel memory of exactly `bufferSize` bytes.
struct PoolBuffer {
    std::uint32_t slot{0};
    std::span<std::byte> data;
};

// Preallocated, ref-counted frame buffers (P20.20).
//
// - One contiguous, 64-byte aligned slab; slot i starts at i * stride(). A later shared-memory
//   pool can use the same layout and hand out offsets instead of pointers.
// - tryAcquire() returns a shared_ptr. Copy it to share the buffer (e.g. as Frame::owner);
//   the slot returns to the pool when the last copy is destroyed.
// - Buffers keep the pool alive, so they may outlive every FramePool handle.
// - Thread-safe. Pixel memory is never allocated after create().
class FramePool : public std::enable_shared_from_this<FramePool> {
    struct PrivateTag {
        explicit PrivateTag() = default;
    };

public:
    static constexpr std::size_t kAlignment = 64;

    // InvalidArgument: bufferCount == 0 or > UINT32_MAX, bufferSize == 0, or size overflow.
    // Internal: out of memory.
    [[nodiscard]] static Result<std::shared_ptr<FramePool>> create(std::size_t bufferCount,
                                                                   std::size_t bufferSize) {
        constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
        if (bufferCount == 0 || bufferCount > std::numeric_limits<std::uint32_t>::max()) {
            return makeError(Errc::InvalidArgument, "FramePool: bufferCount out of range");
        }
        if (bufferSize == 0 || bufferSize > kMax - kAlignment ||
            roundUp(bufferSize) > kMax / bufferCount) {
            return makeError(Errc::InvalidArgument, "FramePool: bufferSize out of range");
        }
        try {
            return std::make_shared<FramePool>(PrivateTag{}, bufferCount, bufferSize);
        } catch (const std::bad_alloc&) {
            return makeError(Errc::Internal, "FramePool: out of memory");
        }
    }

    FramePool(PrivateTag /*tag*/, std::size_t bufferCount, std::size_t bufferSize)
        : bufferSize_{bufferSize}
        , stride_{roundUp(bufferSize)}
        , slabSize_{stride_ * bufferCount}
        , slab_{allocateSlab(slabSize_)} {
        const std::span<std::byte> slab{slab_.get(), slabSize_};
        std::ranges::fill(slab, std::byte{0});
        buffers_.reserve(bufferCount);
        free_.reserve(bufferCount);
        for (std::size_t i = 0; i < bufferCount; ++i) {
            buffers_.push_back(PoolBuffer{.slot = static_cast<std::uint32_t>(i),
                                          .data = slab.subspan(i * stride_, bufferSize_)});
        }
        for (std::size_t i = bufferCount; i > 0; --i) {
            free_.push_back(static_cast<std::uint32_t>(i - 1));
        }
    }

    FramePool(const FramePool&) = delete;
    FramePool& operator=(const FramePool&) = delete;
    FramePool(FramePool&&) = delete;
    FramePool& operator=(FramePool&&) = delete;
    ~FramePool() = default;

    // nullptr when no buffer is free (counted in exhaustedCount()). Never blocks, never throws.
    [[nodiscard]] std::shared_ptr<PoolBuffer> tryAcquire() noexcept {
        std::uint32_t slot = 0;
        {
            const std::lock_guard lock{mutex_};
            if (free_.empty()) {
                ++exhausted_;
                return nullptr;
            }
            slot = free_.back();
            free_.pop_back();
        }
        try {
            return {&buffers_[slot],
                    [self = shared_from_this()](PoolBuffer* b) { self->release(b->slot); }};
        } catch (...) {
            // If the control block allocation throws, shared_ptr already ran the deleter,
            // which returned the slot. Nothing to undo here.
            return nullptr;
        }
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return buffers_.size(); }
    [[nodiscard]] std::size_t bufferSize() const noexcept { return bufferSize_; }
    [[nodiscard]] std::size_t stride() const noexcept { return stride_; }

    [[nodiscard]] std::size_t freeCount() const {
        const std::lock_guard lock{mutex_};
        return free_.size();
    }

    // Number of tryAcquire() calls that found the pool empty.
    [[nodiscard]] std::uint64_t exhaustedCount() const {
        const std::lock_guard lock{mutex_};
        return exhausted_;
    }

    // Whole slab and per-buffer offset, for a later shared-memory pool.
    [[nodiscard]] std::span<std::byte> slab() noexcept { return {slab_.get(), slabSize_}; }
    [[nodiscard]] std::size_t offsetOf(const PoolBuffer& buffer) const noexcept {
        return static_cast<std::size_t>(buffer.slot) * stride_;
    }

private:
    struct SlabDeleter {
        void operator()(std::byte* p) const noexcept {
            ::operator delete(p, std::align_val_t{kAlignment});
        }
    };
    using SlabPtr = std::unique_ptr<std::byte, SlabDeleter>;

    [[nodiscard]] static constexpr std::size_t roundUp(std::size_t n) noexcept {
        return (n + kAlignment - 1) / kAlignment * kAlignment;
    }

    [[nodiscard]] static SlabPtr allocateSlab(std::size_t bytes) {
        return SlabPtr{static_cast<std::byte*>(::operator new(bytes, std::align_val_t{kAlignment}))};
    }

    void release(std::uint32_t slot) noexcept {
        const std::lock_guard lock{mutex_};
        free_.push_back(slot); // capacity reserved in the constructor: no allocation
    }

    std::size_t bufferSize_;
    std::size_t stride_;
    std::size_t slabSize_;
    SlabPtr slab_;
    std::vector<PoolBuffer> buffers_;

    mutable std::mutex mutex_;
    std::vector<std::uint32_t> free_;
    std::uint64_t exhausted_{0};
};

} // namespace vsort::camera

#pragma once

#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace vsort {

// Bounded lock-free MPMC queue (Vyukov). Capacity is rounded up to a power of two (min 2).
// No allocation after construction; tryPush/tryPop never block.
template <typename T>
    requires std::default_initializable<T> && std::movable<T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_{std::bit_ceil(capacity < 2U ? std::size_t{2} : capacity)}
        , mask_{capacity_ - 1U}
        , cells_{std::make_unique<Cell[]>(capacity_)} {
        for (std::size_t i = 0; i < capacity_; ++i) {
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;
    BoundedQueue(BoundedQueue&&) = delete;
    BoundedQueue& operator=(BoundedQueue&&) = delete;
    ~BoundedQueue() = default;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    // Approximate under concurrency; exact when no other thread is active.
    [[nodiscard]] std::size_t sizeApprox() const noexcept {
        const auto enq = enqueuePos_.load(std::memory_order_relaxed);
        const auto deq = dequeuePos_.load(std::memory_order_relaxed);
        return enq >= deq ? enq - deq : 0U;
    }

    // Returns false if the queue is full.
    [[nodiscard]] bool tryPush(T value) {
        Cell* cell = nullptr;
        auto pos = enqueuePos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &cells_[pos & mask_];
            const auto seq = cell->sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::ptrdiff_t>(seq - pos);
            if (diff == 0) {
                if (enqueuePos_.compare_exchange_weak(pos, pos + 1U, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false; // full
            } else {
                pos = enqueuePos_.load(std::memory_order_relaxed);
            }
        }
        cell->value = std::move(value);
        cell->sequence.store(pos + 1U, std::memory_order_release);
        return true;
    }

    // Returns nullopt if the queue is empty.
    [[nodiscard]] std::optional<T> tryPop() {
        Cell* cell = nullptr;
        auto pos = dequeuePos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &cells_[pos & mask_];
            const auto seq = cell->sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::ptrdiff_t>(seq - (pos + 1U));
            if (diff == 0) {
                if (dequeuePos_.compare_exchange_weak(pos, pos + 1U, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return std::nullopt; // empty
            } else {
                pos = dequeuePos_.load(std::memory_order_relaxed);
            }
        }
        std::optional<T> result{std::move(cell->value)};
        cell->sequence.store(pos + mask_ + 1U, std::memory_order_release);
        return result;
    }

private:
    struct Cell {
        std::atomic<std::size_t> sequence{0};
        T value{};
    };

    static constexpr std::size_t kCacheLine = 64;

    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<Cell[]> cells_;
    alignas(kCacheLine) std::atomic<std::size_t> enqueuePos_{0};
    alignas(kCacheLine) std::atomic<std::size_t> dequeuePos_{0};
};

} // namespace vsort

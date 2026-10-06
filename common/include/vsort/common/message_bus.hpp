#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include <vsort/common/bounded_queue.hpp>

namespace vsort {

// One subscriber's inbox for messages of type T. Owned by the subscribing module;
// release the shared_ptr to unsubscribe.
template <typename T>
class Subscription {
public:
    explicit Subscription(std::size_t capacity)
        : queue_{capacity} {}

    [[nodiscard]] std::optional<T> tryPop() { return queue_.tryPop(); }

    // Calls handler(const T&) for up to maxItems messages. Returns the number handled.
    template <typename F>
    std::size_t drain(F&& handler, std::size_t maxItems = std::numeric_limits<std::size_t>::max()) {
        std::size_t handled = 0;
        while (handled < maxItems) {
            auto item = queue_.tryPop();
            if (!item) {
                break;
            }
            handler(*item);
            ++handled;
        }
        return handled;
    }

    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::size_t pending() const noexcept { return queue_.sizeApprox(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return queue_.capacity(); }

    // Used by the bus. Returns false (and counts a drop) when the queue is full.
    bool offer(const T& message) {
        if (queue_.tryPush(message)) {
            return true;
        }
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

private:
    BoundedQueue<T> queue_;
    std::atomic<std::uint64_t> dropped_{0};
};

// Typed publish/subscribe. Messages are copied into each subscriber's bounded queue.
// A full queue drops the NEWEST message for that subscriber only (counted).
class MessageBus {
public:
    template <typename T>
    [[nodiscard]] std::shared_ptr<Subscription<T>> subscribe(std::size_t capacity = 1024) {
        auto sub = std::make_shared<Subscription<T>>(capacity);
        const std::unique_lock lock{mutex_};
        auto& list = subscribers_[std::type_index{typeid(T)}];
        std::erase_if(list, [](const std::weak_ptr<void>& w) { return w.expired(); });
        list.emplace_back(sub);
        return sub;
    }

    // Returns the number of subscribers that accepted the message.
    template <typename T>
        requires std::copy_constructible<T>
    std::size_t publish(const T& message) {
        published_.fetch_add(1, std::memory_order_relaxed);
        std::size_t delivered = 0;
        const std::shared_lock lock{mutex_};
        const auto it = subscribers_.find(std::type_index{typeid(T)});
        if (it == subscribers_.end()) {
            return 0;
        }
        for (const auto& weak : it->second) {
            const auto strong = std::static_pointer_cast<Subscription<T>>(weak.lock());
            if (!strong) {
                continue;
            }
            if (strong->offer(message)) {
                ++delivered;
            } else {
                dropped_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return delivered;
    }

    template <typename T>
    [[nodiscard]] std::size_t subscriberCount() const {
        const std::shared_lock lock{mutex_};
        const auto it = subscribers_.find(std::type_index{typeid(T)});
        if (it == subscribers_.end()) {
            return 0;
        }
        std::size_t count = 0;
        for (const auto& weak : it->second) {
            if (!weak.expired()) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::uint64_t publishedTotal() const noexcept {
        return published_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t droppedTotal() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::type_index, std::vector<std::weak_ptr<void>>> subscribers_;
    std::atomic<std::uint64_t> published_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

} // namespace vsort

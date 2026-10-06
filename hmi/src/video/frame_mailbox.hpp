#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <vsort/ipc/preview_ring.hpp>

namespace vsort::hmi {

using FramePtr = std::shared_ptr<const ipc::PreviewFrame>;

// Latest-frame-wins hand-over between any producer thread and the render thread.
class FrameMailbox {
public:
    void put(FramePtr frame) {
        const std::scoped_lock lock{mutex_};
        frame_ = std::move(frame);
        ++sequence_;
    }

    void clear() { put(nullptr); }

    // nullopt: nothing new since `lastSeen`. Engaged but null: the item was cleared.
    [[nodiscard]] std::optional<FramePtr> takeIfNew(std::uint64_t& lastSeen) const {
        const std::scoped_lock lock{mutex_};
        if (sequence_ == lastSeen) {
            return std::nullopt;
        }
        lastSeen = sequence_;
        return frame_;
    }

    [[nodiscard]] FramePtr latest() const {
        const std::scoped_lock lock{mutex_};
        return frame_;
    }

private:
    mutable std::mutex mutex_;
    FramePtr frame_;
    std::uint64_t sequence_{0};
};

} // namespace vsort::hmi

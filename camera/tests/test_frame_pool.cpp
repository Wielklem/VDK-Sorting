#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <vsort/camera/camera.hpp>
#include <vsort/camera/frame_pool.hpp>

namespace {

using namespace vsort;
using namespace vsort::camera;

std::shared_ptr<FramePool> makePool(std::size_t count, std::size_t size) {
    auto pool = FramePool::create(count, size);
    EXPECT_TRUE(pool.has_value());
    return pool ? *pool : nullptr;
}

TEST(FramePool, RejectsInvalidArguments) {
    constexpr auto kMax = std::numeric_limits<std::size_t>::max();
    for (const auto& r : {FramePool::create(0, 16), FramePool::create(4, 0),
                          FramePool::create(4, kMax), FramePool::create(kMax, 16)}) {
        ASSERT_FALSE(r.has_value());
        EXPECT_EQ(r.error().code, Errc::InvalidArgument);
    }
}

TEST(FramePool, GeometryAndAlignment) {
    auto pool = makePool(3, 100);
    ASSERT_NE(pool, nullptr);
    EXPECT_EQ(pool->capacity(), 3U);
    EXPECT_EQ(pool->bufferSize(), 100U);
    EXPECT_EQ(pool->stride(), 128U);
    EXPECT_EQ(pool->slab().size(), 3U * 128U);

    std::vector<std::shared_ptr<PoolBuffer>> held;
    std::set<std::uint32_t> slots;
    for (int i = 0; i < 3; ++i) {
        held.push_back(pool->tryAcquire());
        ASSERT_NE(held.back(), nullptr);
        slots.insert(held.back()->slot);
    }
    EXPECT_EQ(slots.size(), 3U);
    for (const auto& b : held) {
        EXPECT_EQ(b->data.size(), 100U);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(b->data.data()) % FramePool::kAlignment, 0U);
        EXPECT_EQ(b->data.data(), pool->slab().data() + pool->offsetOf(*b));
    }
}

TEST(FramePool, ExhaustionAndRecovery) {
    auto pool = makePool(2, 16);
    ASSERT_NE(pool, nullptr);
    auto a = pool->tryAcquire();
    auto b = pool->tryAcquire();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(pool->freeCount(), 0U);

    EXPECT_EQ(pool->tryAcquire(), nullptr);
    EXPECT_EQ(pool->exhaustedCount(), 1U);

    a.reset();
    EXPECT_EQ(pool->freeCount(), 1U);
    EXPECT_NE(pool->tryAcquire(), nullptr);
}

TEST(FramePool, SlotStaysBusyUntilLastCopyIsReleased) {
    auto pool = makePool(1, 16);
    ASSERT_NE(pool, nullptr);
    auto a = pool->tryAcquire();
    auto b = a;
    a.reset();
    EXPECT_EQ(pool->freeCount(), 0U);
    b.reset();
    EXPECT_EQ(pool->freeCount(), 1U);
}

TEST(FramePool, BufferOutlivesPoolHandle) {
    std::shared_ptr<PoolBuffer> buf;
    {
        auto pool = makePool(1, 16);
        ASSERT_NE(pool, nullptr);
        buf = pool->tryAcquire();
    }
    ASSERT_NE(buf, nullptr);
    buf->data[0] = std::byte{42};
    EXPECT_EQ(buf->data[0], std::byte{42});
}

TEST(FramePool, WorksAsFrameOwner) {
    auto pool = makePool(1, 64);
    ASSERT_NE(pool, nullptr);
    auto buf = pool->tryAcquire();
    ASSERT_NE(buf, nullptr);

    Frame frame;
    frame.owner = buf;
    frame.data = buf->data;
    buf.reset();
    EXPECT_EQ(pool->freeCount(), 0U);

    frame = Frame{};
    EXPECT_EQ(pool->freeCount(), 1U);
}

TEST(FramePool, ConcurrentAcquireReleaseKeepsBuffersExclusive) {
    auto pool = makePool(4, 64);
    ASSERT_NE(pool, nullptr);
    std::atomic<bool> corrupted{false};
    std::atomic<std::uint64_t> acquired{0};

    auto worker = [&](std::uint8_t id) {
        for (int i = 0; i < 20000; ++i) {
            auto buf = pool->tryAcquire();
            if (!buf) {
                std::this_thread::yield();
                continue;
            }
            ++acquired;
            std::ranges::fill(buf->data, static_cast<std::byte>(id));
            std::this_thread::yield();
            if (!std::ranges::all_of(buf->data,
                                     [id](std::byte v) { return v == static_cast<std::byte>(id); })) {
                corrupted = true;
            }
        }
    };

    std::vector<std::thread> threads;
    for (std::uint8_t id = 1; id <= 8; ++id) {
        threads.emplace_back(worker, id);
    }
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_FALSE(corrupted.load());
    EXPECT_GT(acquired.load(), 0U);
    EXPECT_EQ(pool->freeCount(), pool->capacity());
}

} // namespace

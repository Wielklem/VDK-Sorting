#include <array>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <vsort/common/bounded_queue.hpp>

TEST(BoundedQueue, CapacityRoundsUpToPowerOfTwo) {
    EXPECT_EQ(vsort::BoundedQueue<int>{5}.capacity(), 8U);
    EXPECT_EQ(vsort::BoundedQueue<int>{0}.capacity(), 2U);
    EXPECT_EQ(vsort::BoundedQueue<int>{16}.capacity(), 16U);
}

TEST(BoundedQueue, FifoFullEmpty) {
    vsort::BoundedQueue<int> q{4};
    EXPECT_FALSE(q.tryPop().has_value());
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(q.tryPush(i));
    }
    EXPECT_FALSE(q.tryPush(99));
    for (int i = 0; i < 4; ++i) {
        const auto v = q.tryPop();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, i);
    }
    EXPECT_FALSE(q.tryPop().has_value());
}

TEST(BoundedQueue, WrapsAround) {
    vsort::BoundedQueue<int> q{4};
    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(q.tryPush(i));
        const auto v = q.tryPop();
        ASSERT_TRUE(v.has_value());
        ASSERT_EQ(*v, i);
    }
}

TEST(BoundedQueue, MoveOnlyType) {
    vsort::BoundedQueue<std::unique_ptr<int>> q{2};
    EXPECT_TRUE(q.tryPush(std::make_unique<int>(5)));
    const auto v = q.tryPop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(**v, 5);
}

TEST(BoundedQueue, MultiProducerSingleConsumerKeepsPerProducerOrder) {
    constexpr std::uint64_t kProducers = 4;
    constexpr std::uint64_t kPerProducer = 20'000;

    vsort::BoundedQueue<std::uint64_t> q{256};
    std::vector<std::jthread> producers;
    for (std::uint64_t p = 0; p < kProducers; ++p) {
        producers.emplace_back([&q, p] {
            for (std::uint64_t i = 0; i < kPerProducer;) {
                if (q.tryPush((p << 32U) | i)) {
                    ++i;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::array<std::uint64_t, kProducers> next{};
    std::uint64_t received = 0;
    while (received < kProducers * kPerProducer) {
        const auto v = q.tryPop();
        if (!v) {
            std::this_thread::yield();
            continue;
        }
        const auto p = *v >> 32U;
        const auto i = *v & 0xFFFFFFFFU;
        ASSERT_LT(p, kProducers);
        EXPECT_EQ(i, next[p]);
        ++next[p];
        ++received;
    }
    EXPECT_FALSE(q.tryPop().has_value());
}

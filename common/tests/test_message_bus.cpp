#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <vsort/common/message_bus.hpp>

namespace {

struct Ping {
    int value{};
};
struct Pong {
    int value{};
};

} // namespace

TEST(MessageBus, DeliversToAllSubscribersOfType) {
    vsort::MessageBus bus;
    auto a = bus.subscribe<Ping>(8);
    auto b = bus.subscribe<Ping>(8);
    EXPECT_EQ(bus.publish(Ping{7}), 2U);
    const auto va = a->tryPop();
    const auto vb = b->tryPop();
    ASSERT_TRUE(va.has_value());
    ASSERT_TRUE(vb.has_value());
    EXPECT_EQ(va->value, 7);
    EXPECT_EQ(vb->value, 7);
}

TEST(MessageBus, TypesAreIsolated) {
    vsort::MessageBus bus;
    auto pongs = bus.subscribe<Pong>(8);
    EXPECT_EQ(bus.publish(Ping{1}), 0U);
    EXPECT_FALSE(pongs->tryPop().has_value());
    EXPECT_EQ(bus.publish(Pong{2}), 1U);
    EXPECT_TRUE(pongs->tryPop().has_value());
}

TEST(MessageBus, CountsDropsWhenFull) {
    vsort::MessageBus bus;
    auto sub = bus.subscribe<Ping>(2);
    for (int i = 0; i < 5; ++i) {
        bus.publish(Ping{i});
    }
    EXPECT_EQ(sub->dropped(), 3U);
    EXPECT_EQ(bus.droppedTotal(), 3U);
    EXPECT_EQ(bus.publishedTotal(), 5U);
    // Oldest are kept, newest dropped.
    EXPECT_EQ(sub->tryPop()->value, 0);
    EXPECT_EQ(sub->tryPop()->value, 1);
    EXPECT_FALSE(sub->tryPop().has_value());
}

TEST(MessageBus, SlowSubscriberDoesNotBlockOthers) {
    vsort::MessageBus bus;
    auto slow = bus.subscribe<Ping>(2);
    auto fast = bus.subscribe<Ping>(16);
    for (int i = 0; i < 10; ++i) {
        bus.publish(Ping{i});
    }
    EXPECT_EQ(fast->pending(), 10U);
    EXPECT_EQ(fast->dropped(), 0U);
    EXPECT_EQ(slow->dropped(), 8U);
}

TEST(MessageBus, ReleasingSubscriptionUnsubscribes) {
    vsort::MessageBus bus;
    auto sub = bus.subscribe<Ping>(4);
    EXPECT_EQ(bus.subscriberCount<Ping>(), 1U);
    sub.reset();
    EXPECT_EQ(bus.subscriberCount<Ping>(), 0U);
    EXPECT_EQ(bus.publish(Ping{1}), 0U);
}

TEST(MessageBus, DrainHonoursMaxItems) {
    vsort::MessageBus bus;
    auto sub = bus.subscribe<Ping>(8);
    for (int i = 0; i < 5; ++i) {
        bus.publish(Ping{i});
    }
    std::vector<int> seen;
    EXPECT_EQ(sub->drain([&](const Ping& p) { seen.push_back(p.value); }, 3), 3U);
    EXPECT_EQ(sub->drain([&](const Ping& p) { seen.push_back(p.value); }), 2U);
    EXPECT_EQ(seen, (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST(MessageBus, ConcurrentPublishersLoseNothingWhenQueueLargeEnough) {
    constexpr int kPublishers = 4;
    constexpr int kPerPublisher = 5000;

    vsort::MessageBus bus;
    auto sub = bus.subscribe<Ping>(32768);
    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < kPublishers; ++t) {
            threads.emplace_back([&bus] {
                for (int i = 0; i < kPerPublisher; ++i) {
                    bus.publish(Ping{i});
                }
            });
        }
    }
    EXPECT_EQ(sub->dropped(), 0U);
    EXPECT_EQ(sub->drain([](const Ping&) {}),
              static_cast<std::size_t>(kPublishers) * kPerPublisher);
    EXPECT_EQ(bus.publishedTotal(), static_cast<std::uint64_t>(kPublishers) * kPerPublisher);
}

TEST(MessageBus, SubscribeWhilePublishing) {
    vsort::MessageBus bus;
    std::atomic<bool> stop{false};
    std::jthread publisher{[&] {
        while (!stop.load()) {
            bus.publish(Ping{1});
            std::this_thread::sleep_for(std::chrono::microseconds{50});
        }
    }};
    for (int i = 0; i < 200; ++i) {
        auto sub = bus.subscribe<Ping>(4);
        static_cast<void>(sub->tryPop());
    }
    stop.store(true);
}

TEST(MessageBus, CapacityRoundsUpToPowerOfTwo) {
    vsort::MessageBus bus;
    const auto sub = bus.subscribe<Ping>(3);
    EXPECT_EQ(sub->capacity(), 4U);
}

TEST(MessageBus, DrainOnEmptyReturnsZero) {
    vsort::MessageBus bus;
    const auto sub = bus.subscribe<Ping>(4);
    EXPECT_EQ(sub->drain([](const Ping&) {}), 0U);
}

TEST(MessageBus, PublishWithoutSubscribersIsCountedNotDropped) {
    vsort::MessageBus bus;
    EXPECT_EQ(bus.publish(Ping{1}), 0U);
    EXPECT_EQ(bus.publishedTotal(), 1U);
    EXPECT_EQ(bus.droppedTotal(), 0U);
}

TEST(MessageBus, ExpiredSubscribersAreNotCounted) {
    vsort::MessageBus bus;
    auto first = bus.subscribe<Ping>(4);
    const auto second = bus.subscribe<Ping>(4);
    first.reset();
    EXPECT_EQ(bus.subscriberCount<Ping>(), 1U);
    EXPECT_EQ(bus.publish(Ping{1}), 1U);
}

TEST(MessageBus, ReceivedPlusDroppedEqualsPublishedUnderContention) {
    constexpr int kPublishers = 4;
    constexpr int kPerPublisher = 2000;

    vsort::MessageBus bus;
    auto sub = bus.subscribe<Ping>(64);
    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < kPublishers; ++t) {
            threads.emplace_back([&bus] {
                for (int i = 0; i < kPerPublisher; ++i) {
                    bus.publish(Ping{i});
                }
            });
        }
    }
    const auto received = static_cast<std::uint64_t>(sub->drain([](const Ping&) {}));
    EXPECT_EQ(received + sub->dropped(), static_cast<std::uint64_t>(kPublishers) * kPerPublisher);
}

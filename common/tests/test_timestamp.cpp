#include <chrono>

#include <gtest/gtest.h>

#include <vsort/common/timestamp.hpp>

TEST(Timestamp, Monotonic) {
    const auto a = vsort::Timestamp::now();
    const auto b = vsort::Timestamp::now();
    EXPECT_LE(a, b);
    EXPECT_GE((b - a).count(), 0);
}

TEST(WallTime, Iso8601Epoch) {
    const vsort::WallTime t{std::chrono::seconds{0}};
    EXPECT_EQ(t.toIso8601(), "1970-01-01T00:00:00.000000Z");
}

TEST(WallTime, Iso8601Micros) {
    const vsort::WallTime t{std::chrono::seconds{86'400} + std::chrono::microseconds{123'456}};
    EXPECT_EQ(t.toIso8601(), "1970-01-02T00:00:00.123456Z");
}

#include <cstdint>
#include <type_traits>
#include <unordered_set>

#include <gtest/gtest.h>

#include <vsort/common/types.hpp>

static_assert(!std::is_convertible_v<vsort::FrameId, vsort::ObjectId>);
static_assert(!std::is_convertible_v<std::uint64_t, vsort::FrameId>);
static_assert(sizeof(vsort::FrameId) == sizeof(std::uint64_t));

TEST(StrongId, Compares) {
    const vsort::FrameId a{1};
    const vsort::FrameId b{2};
    EXPECT_LT(a, b);
    EXPECT_EQ(a, vsort::FrameId{1});
    EXPECT_EQ(b.value(), 2U);
}

TEST(StrongId, Hashes) {
    const std::unordered_set<vsort::ObjectId> set{vsort::ObjectId{7}};
    EXPECT_TRUE(set.contains(vsort::ObjectId{7}));
    EXPECT_FALSE(set.contains(vsort::ObjectId{8}));
}

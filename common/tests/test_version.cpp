#include <expected>

#include <gtest/gtest.h>

#include <vsort/common/version.hpp>

TEST(Version, NotEmpty) {
    EXPECT_FALSE(vsort::version().empty());
}

TEST(Toolchain, StdExpectedAvailable) {
    std::expected<int, int> value{42};
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 42);
}

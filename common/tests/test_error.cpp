#include <gtest/gtest.h>

#include <vsort/common/error.hpp>

namespace {

vsort::Result<int> parsePositive(int v) {
    if (v <= 0) {
        return vsort::makeError(vsort::Errc::InvalidArgument, "must be > 0");
    }
    return v;
}

} // namespace

TEST(Error, ValuePath) {
    const auto r = parsePositive(5);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 5);
}

TEST(Error, ErrorPath) {
    const auto r = parsePositive(-1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, vsort::Errc::InvalidArgument);
    EXPECT_EQ(r.error().what(), "invalid_argument: must be > 0");
}

TEST(Error, VoidResult) {
    const vsort::Result<> ok{};
    EXPECT_TRUE(ok.has_value());
}

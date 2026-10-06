#include <array>
#include <bit>
#include <cstdint>

#include <gtest/gtest.h>

#include <vsort/common/byte_order.hpp>

TEST(ByteOrder, RoundTrip) {
    constexpr std::uint32_t v = 0x11223344U;
    EXPECT_EQ(vsort::fromLittleEndian(vsort::toLittleEndian(v)), v);
}

TEST(ByteOrder, LittleEndianLayout) {
    const auto le = vsort::toLittleEndian(std::uint32_t{0x11223344U});
    const auto bytes = std::bit_cast<std::array<std::uint8_t, 4>>(le);
    EXPECT_EQ(bytes[0], 0x44U);
    EXPECT_EQ(bytes[3], 0x11U);
}

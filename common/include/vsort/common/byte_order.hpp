#pragma once

#include <bit>
#include <concepts>

namespace vsort {

// Wire/file format (M45, recordings, DB blobs) is little-endian.
static_assert(std::endian::native == std::endian::little ||
              std::endian::native == std::endian::big,
              "mixed-endian platforms are not supported");

template <std::integral T>
[[nodiscard]] constexpr T toLittleEndian(T value) noexcept {
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(value);
    } else {
        return value;
    }
}

template <std::integral T>
[[nodiscard]] constexpr T fromLittleEndian(T value) noexcept {
    return toLittleEndian(value);
}

} // namespace vsort

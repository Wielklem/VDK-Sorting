#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

#include <vsort/common/byte_order.hpp>

namespace vsort::recorder {

// Recording file, one per camera: camNN.vrec. All integers little-endian.
//
// File header, 16 bytes:
//    0  'V' 'R' 'E' 'C'
//    4  u16 version
//    6  u16 cameraIndex
//    8  8 bytes reserved (0)
// Then, per frame: record header (48 bytes) followed by payloadBytes of raw pixel data.
//
// Record header, 48 bytes:
//    0  u64 frameId
//    8  i64 hostTimestampNs    monotonic clock, only differences are meaningful
//   16  u64 deviceTimestampNs  0 = unavailable
//   24  u32 width
//   28  u32 height
//   32  u32 strideBytes
//   36  u32 payloadBytes
//   40  u8  pixelFormat        camera::PixelFormat value
//   41  7 bytes reserved (0)
inline constexpr std::uint16_t kFormatVersion = 1;
inline constexpr std::size_t kFileHeaderSize = 16;
inline constexpr std::size_t kRecordHeaderSize = 48;
inline constexpr std::array<std::byte, 4> kMagic{std::byte{'V'}, std::byte{'R'}, std::byte{'E'},
                                                 std::byte{'C'}};

using FileHeaderBytes = std::array<std::byte, kFileHeaderSize>;
using RecordHeaderBytes = std::array<std::byte, kRecordHeaderSize>;

struct FileHeader {
    std::uint16_t version{kFormatVersion};
    std::uint16_t cameraIndex{0};
};

struct RecordHeader {
    std::uint64_t frameId{0};
    std::int64_t hostTimestampNs{0};
    std::uint64_t deviceTimestampNs{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t strideBytes{0};
    std::uint32_t payloadBytes{0};
    std::uint8_t pixelFormat{0};
};

namespace detail {

template <std::integral T>
void put(std::span<std::byte> out, std::size_t offset, T value) noexcept {
    const T le = toLittleEndian(value);
    std::memcpy(out.data() + offset, &le, sizeof(T));
}

template <std::integral T>
[[nodiscard]] T get(std::span<const std::byte> in, std::size_t offset) noexcept {
    T le{};
    std::memcpy(&le, in.data() + offset, sizeof(T));
    return fromLittleEndian(le);
}

} // namespace detail

[[nodiscard]] inline FileHeaderBytes encodeFileHeader(const FileHeader& header) noexcept {
    FileHeaderBytes out{};
    std::ranges::copy(kMagic, out.begin());
    detail::put(out, 4, header.version);
    detail::put(out, 6, header.cameraIndex);
    return out;
}

// nullopt if the input is too short or the magic is wrong. The caller checks the version.
[[nodiscard]] inline std::optional<FileHeader>
decodeFileHeader(std::span<const std::byte> in) noexcept {
    if (in.size() < kFileHeaderSize || !std::equal(kMagic.begin(), kMagic.end(), in.begin())) {
        return std::nullopt;
    }
    return FileHeader{.version = detail::get<std::uint16_t>(in, 4),
                      .cameraIndex = detail::get<std::uint16_t>(in, 6)};
}

[[nodiscard]] inline RecordHeaderBytes encodeRecordHeader(const RecordHeader& header) noexcept {
    RecordHeaderBytes out{};
    detail::put(out, 0, header.frameId);
    detail::put(out, 8, header.hostTimestampNs);
    detail::put(out, 16, header.deviceTimestampNs);
    detail::put(out, 24, header.width);
    detail::put(out, 28, header.height);
    detail::put(out, 32, header.strideBytes);
    detail::put(out, 36, header.payloadBytes);
    detail::put(out, 40, header.pixelFormat);
    return out;
}

// nullopt if the input is shorter than a record header.
[[nodiscard]] inline std::optional<RecordHeader>
decodeRecordHeader(std::span<const std::byte> in) noexcept {
    if (in.size() < kRecordHeaderSize) {
        return std::nullopt;
    }
    return RecordHeader{.frameId = detail::get<std::uint64_t>(in, 0),
                        .hostTimestampNs = detail::get<std::int64_t>(in, 8),
                        .deviceTimestampNs = detail::get<std::uint64_t>(in, 16),
                        .width = detail::get<std::uint32_t>(in, 24),
                        .height = detail::get<std::uint32_t>(in, 28),
                        .strideBytes = detail::get<std::uint32_t>(in, 32),
                        .payloadBytes = detail::get<std::uint32_t>(in, 36),
                        .pixelFormat = detail::get<std::uint8_t>(in, 40)};
}

} // namespace vsort::recorder

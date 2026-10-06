#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <vsort/ipc/preview_ring.hpp>

#include "test_util.hpp"

using namespace vsort;
using namespace vsort::ipc;

namespace {

PreviewFrameInfo infoFor(std::uint64_t id, std::uint32_t w, std::uint32_t h) {
    return PreviewFrameInfo{.frameId = id,
                            .timestampNs = id * 10,
                            .width = w,
                            .height = h,
                            .strideBytes = w,
                            .pixelFormat = PreviewPixelFormat::Mono8};
}

std::vector<std::byte> filled(std::size_t n, std::uint8_t v) {
    return std::vector<std::byte>(n, static_cast<std::byte>(v));
}

} // namespace

TEST(PreviewRing, NameContainsCameraAndGeneration) {
    EXPECT_EQ(previewRingName(3, 2), "vdk_preview_3_2");
    EXPECT_TRUE(platform::isValidSharedMemoryName(previewRingName(65535, 4000000000U)));
}

TEST(PreviewRing, WriteThenReadNewest) {
    auto writer = PreviewRingWriter::create("vsort_test_ring_basic",
                                            {.slotCount = 4, .slotSize = 64, .generation = 5});
    VSORT_SKIP_IF_NOT_SUPPORTED(writer);
    ASSERT_TRUE(writer.has_value()) << writer.error().what();

    auto reader = PreviewRingReader::open("vsort_test_ring_basic");
    ASSERT_TRUE(reader.has_value()) << reader.error().what();
    EXPECT_EQ((*reader)->generation(), 5U);
    EXPECT_EQ((*reader)->slotSize(), 64U);

    std::uint64_t seen = 0;
    EXPECT_FALSE((*reader)->readNewest(seen).has_value()); // nothing written yet

    ASSERT_TRUE((*writer)->write(infoFor(1, 8, 4), filled(32, 0x11)).has_value());
    const auto first = (*reader)->readNewest(seen);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->info.frameId, 1U);
    EXPECT_EQ(first->info.width, 8U);
    EXPECT_EQ(first->pixels, filled(32, 0x11));
    EXPECT_FALSE((*reader)->readNewest(seen).has_value()); // same frame is not delivered twice
}

TEST(PreviewRing, ReaderOnlyGetsNewestAfterManyWrites) {
    auto writer =
        PreviewRingWriter::create("vsort_test_ring_wrap", {.slotCount = 3, .slotSize = 16});
    VSORT_SKIP_IF_NOT_SUPPORTED(writer);
    ASSERT_TRUE(writer.has_value());
    auto reader = PreviewRingReader::open("vsort_test_ring_wrap");
    ASSERT_TRUE(reader.has_value());

    for (std::uint64_t id = 1; id <= 10; ++id) {
        ASSERT_TRUE((*writer)
                        ->write(infoFor(id, 4, 4), filled(16, static_cast<std::uint8_t>(id)))
                        .has_value());
    }
    std::uint64_t seen = 0;
    const auto frame = (*reader)->readNewest(seen);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->info.frameId, 10U);
    EXPECT_EQ(frame->pixels, filled(16, 10));
    EXPECT_EQ((*writer)->framesWritten(), 10U);
}

TEST(PreviewRing, RejectsOversizedFrameAndBadSpec) {
    auto writer = PreviewRingWriter::create("vsort_test_ring_big", {.slotCount = 2, .slotSize = 8});
    VSORT_SKIP_IF_NOT_SUPPORTED(writer);
    ASSERT_TRUE(writer.has_value());
    const auto bad = (*writer)->write(infoFor(1, 3, 3), filled(9, 1));
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, Errc::InvalidArgument);

    const auto zero =
        PreviewRingWriter::create("vsort_test_ring_zero", {.slotCount = 0, .slotSize = 8});
    ASSERT_FALSE(zero.has_value());
    EXPECT_EQ(zero.error().code, Errc::InvalidArgument);
}

TEST(PreviewRing, OpenMissingOrForeignMemoryFails) {
    const auto missing = PreviewRingReader::open("vsort_test_ring_missing");
    ASSERT_FALSE(missing.has_value());
    EXPECT_NE(missing.error().code, Errc::Internal);

    auto foreign = platform::createSharedMemory("vsort_test_ring_foreign", 4096);
    VSORT_SKIP_IF_NOT_SUPPORTED(foreign);
    ASSERT_TRUE(foreign.has_value());
    const auto reader = PreviewRingReader::open("vsort_test_ring_foreign");
    ASSERT_FALSE(reader.has_value());
    EXPECT_EQ(reader.error().code, Errc::ValidationFailed);
}

TEST(PreviewRing, RingIsGoneWhenWriterIsDestroyed) {
    {
        auto writer =
            PreviewRingWriter::create("vsort_test_ring_gone", {.slotCount = 2, .slotSize = 8});
        VSORT_SKIP_IF_NOT_SUPPORTED(writer);
        ASSERT_TRUE(writer.has_value());
    }
    EXPECT_FALSE(PreviewRingReader::open("vsort_test_ring_gone").has_value());
}

// A reader must never see a torn frame: every byte of a frame equals its frame id (mod 256).
TEST(PreviewRing, ConcurrentReaderNeverSeesTornFrames) {
    constexpr std::uint32_t kSize = 4096;
    auto writer =
        PreviewRingWriter::create("vsort_test_ring_race", {.slotCount = 4, .slotSize = kSize});
    VSORT_SKIP_IF_NOT_SUPPORTED(writer);
    ASSERT_TRUE(writer.has_value());
    auto reader = PreviewRingReader::open("vsort_test_ring_race");
    ASSERT_TRUE(reader.has_value());

    std::atomic<bool> stop{false};
    std::thread producer{[&] {
        std::uint64_t id = 1;
        while (!stop.load()) {
            (void)(*writer)->write(infoFor(id, 64, 64),
                                   filled(kSize, static_cast<std::uint8_t>(id & 0xFFU)));
            ++id;
            std::this_thread::sleep_for(std::chrono::microseconds{20});
        }
    }};

    std::uint64_t seen = 0;
    std::uint64_t received = 0;
    std::uint64_t torn = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (received < 2000 && std::chrono::steady_clock::now() < deadline) {
        const auto frame = (*reader)->readNewest(seen);
        if (!frame) {
            continue;
        }
        ++received;
        const auto expected = static_cast<std::byte>(frame->info.frameId & 0xFFU);
        for (const std::byte b : frame->pixels) {
            if (b != expected) {
                ++torn;
                break;
            }
        }
    }
    stop = true;
    producer.join();
    EXPECT_GE(received, 100U);
    EXPECT_EQ(torn, 0U);
}

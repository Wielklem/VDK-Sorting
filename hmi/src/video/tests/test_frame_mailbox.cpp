#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include "video/frame_mailbox.hpp"

namespace {

using vsort::hmi::FrameMailbox;
using vsort::hmi::FramePtr;

FramePtr frameWithId(std::uint64_t id) {
    auto frame = std::make_shared<vsort::ipc::PreviewFrame>();
    frame->info.frameId = id;
    return frame;
}

} // namespace

TEST(FrameMailbox, EmptyHasNothingNew) {
    const FrameMailbox box;
    std::uint64_t seen = 0;
    EXPECT_FALSE(box.takeIfNew(seen).has_value());
    EXPECT_EQ(box.latest(), nullptr);
}

TEST(FrameMailbox, DeliversEachFrameOnce) {
    FrameMailbox box;
    std::uint64_t seen = 0;
    box.put(frameWithId(1));

    const auto first = box.takeIfNew(seen);
    ASSERT_TRUE(first.has_value());
    ASSERT_NE(*first, nullptr);
    EXPECT_EQ((*first)->info.frameId, 1U);
    EXPECT_FALSE(box.takeIfNew(seen).has_value());
}

TEST(FrameMailbox, NewestWins) {
    FrameMailbox box;
    std::uint64_t seen = 0;
    box.put(frameWithId(1));
    box.put(frameWithId(2));
    box.put(frameWithId(3));

    const auto got = box.takeIfNew(seen);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ((*got)->info.frameId, 3U);
    EXPECT_EQ(box.latest()->info.frameId, 3U);
}

TEST(FrameMailbox, ClearIsReportedOnce) {
    FrameMailbox box;
    std::uint64_t seen = 0;
    box.put(frameWithId(1));
    ASSERT_TRUE(box.takeIfNew(seen).has_value());

    box.clear();
    const auto cleared = box.takeIfNew(seen);
    ASSERT_TRUE(cleared.has_value());
    EXPECT_EQ(*cleared, nullptr);
    EXPECT_FALSE(box.takeIfNew(seen).has_value());
    EXPECT_EQ(box.latest(), nullptr);
}

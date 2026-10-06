#include <chrono>

#include <gtest/gtest.h>

#include <vsort/camera/health.hpp>

using namespace vsort;
using namespace vsort::camera;
using namespace std::chrono_literals;
using Kind = FrameIdTracker::Kind;

TEST(FrameIdTracker, FirstFrameSetsBaseline) {
    FrameIdTracker tracker;
    EXPECT_EQ(tracker.observe(FrameId{500}).kind, Kind::First);
    EXPECT_EQ(tracker.observe(FrameId{501}).kind, Kind::InOrder);
}

TEST(FrameIdTracker, GapReportsMissedFrames) {
    FrameIdTracker tracker;
    static_cast<void>(tracker.observe(FrameId{1}));
    const auto gap = tracker.observe(FrameId{5});
    EXPECT_EQ(gap.kind, Kind::Gap);
    EXPECT_EQ(gap.missed, 3U);
    EXPECT_EQ(tracker.observe(FrameId{6}).kind, Kind::InOrder);
}

TEST(FrameIdTracker, RepeatedOrBackwardsIsReset) {
    FrameIdTracker tracker;
    static_cast<void>(tracker.observe(FrameId{10}));
    EXPECT_EQ(tracker.observe(FrameId{10}).kind, Kind::Reset);
    EXPECT_EQ(tracker.observe(FrameId{3}).kind, Kind::Reset);
    EXPECT_EQ(tracker.observe(FrameId{4}).kind, Kind::InOrder); // new baseline is 3
}

TEST(FrameIdTracker, ResetForgetsBaseline) {
    FrameIdTracker tracker;
    static_cast<void>(tracker.observe(FrameId{10}));
    tracker.reset();
    EXPECT_EQ(tracker.observe(FrameId{0}).kind, Kind::First);
}

TEST(ReconnectBackoff, DoublesUpToTheCap) {
    ReconnectBackoff backoff{100ms, 350ms};
    EXPECT_EQ(backoff.next(), 100ms);
    EXPECT_EQ(backoff.next(), 200ms);
    EXPECT_EQ(backoff.next(), 350ms);
    EXPECT_EQ(backoff.next(), 350ms);
    backoff.reset();
    EXPECT_EQ(backoff.next(), 100ms);
}

TEST(ReconnectBackoff, InvalidBoundsAreClamped) {
    ReconnectBackoff backoff{0ms, 0ms};
    EXPECT_EQ(backoff.next(), 1ms);
    EXPECT_EQ(backoff.next(), 1ms);
}

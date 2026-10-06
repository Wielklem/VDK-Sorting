#include <gtest/gtest.h>

#include <vsort/camera/genicam.hpp>

using namespace vsort::camera;

TEST(TriggerPlan, HardwareUsesLine0AndTheChosenEdge) {
    const auto rising = triggerPlan(TriggerMode::Hardware, TriggerEdge::Rising);
    EXPECT_TRUE(rising.triggerOn);
    EXPECT_EQ(rising.source, "Line0");
    EXPECT_EQ(rising.activation, "RisingEdge");
    EXPECT_EQ(triggerPlan(TriggerMode::Hardware, TriggerEdge::Falling).activation, "FallingEdge");
}

TEST(TriggerPlan, SoftwareHasNoActivation) {
    const auto plan = triggerPlan(TriggerMode::Software, TriggerEdge::Rising);
    EXPECT_TRUE(plan.triggerOn);
    EXPECT_EQ(plan.source, "Software");
    EXPECT_TRUE(plan.activation.empty());
}

TEST(TriggerPlan, FreeRunTurnsTheTriggerOff) {
    const auto plan = triggerPlan(TriggerMode::FreeRun, TriggerEdge::Rising);
    EXPECT_FALSE(plan.triggerOn);
    EXPECT_TRUE(plan.source.empty());
    EXPECT_TRUE(plan.activation.empty());
}

TEST(TicksToNs, OneGigahertzIsIdentity) {
    EXPECT_EQ(ticksToNs(123'456'789, 1'000'000'000), 123'456'789U);
}

TEST(TicksToNs, ScalesOtherFrequencies) {
    EXPECT_EQ(ticksToNs(1000, 125'000'000), 8000U);                 // 8 ns per tick
    EXPECT_EQ(ticksToNs(525'000'000, 100'000'000), 5'250'000'000U); // 5.25 s
}

TEST(TicksToNs, UnknownFrequencyGivesZero) {
    EXPECT_EQ(ticksToNs(42, 0), 0U);
}

TEST(TicksToNs, LargeCountersDoNotOverflow) {
    EXPECT_EQ(ticksToNs(1'000'000'000'000'000ULL, 100'000'000), 10'000'000'000'000'000ULL);
}

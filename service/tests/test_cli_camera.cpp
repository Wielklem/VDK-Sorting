#include <initializer_list>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "cli.hpp"

using namespace vsort;
using namespace vsort::service;

namespace {

Result<Options> parse(std::initializer_list<std::string_view> args) {
    const std::vector<std::string_view> list{args};
    return parseArgs(list);
}

} // namespace

TEST(CliCamera, DefaultsToAutomaticSourceWithoutOverrides) {
    const auto options = parse({});
    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->cameraSource, CameraSource::Auto);
    EXPECT_FALSE(options->freeRun);
    EXPECT_FALSE(options->replayLoop);
}

TEST(CliCamera, ReplayImpliesTheReplaySource) {
    const auto options = parse(
        {"--replay", "/data/session1", "--replay-speed", "2.5", "--replay-loop", "--free-run"});
    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->cameraSource, CameraSource::Replay);
    EXPECT_EQ(options->replayDir, std::filesystem::path{"/data/session1"});
    EXPECT_DOUBLE_EQ(options->replaySpeed, 2.5);
    EXPECT_TRUE(options->replayLoop);
    EXPECT_TRUE(options->freeRun);
}

TEST(CliCamera, SourceCanBeChosenExplicitly) {
    const auto none = parse({"--camera-source=none"});
    ASSERT_TRUE(none.has_value());
    EXPECT_EQ(none->cameraSource, CameraSource::None);
    const auto daheng = parse({"--camera-source", "daheng"});
    ASSERT_TRUE(daheng.has_value());
    EXPECT_EQ(daheng->cameraSource, CameraSource::Daheng);
}

TEST(CliCamera, RejectsBadCombinationsAndValues) {
    EXPECT_FALSE(parse({"--camera-source", "bogus"}).has_value());
    EXPECT_FALSE(parse({"--camera-source", "replay"}).has_value()); // needs --replay
    EXPECT_FALSE(parse({"--camera-source", "daheng", "--replay", "/x"}).has_value());
    EXPECT_FALSE(parse({"--replay", "/x", "--replay-speed", "0"}).has_value());
    EXPECT_FALSE(parse({"--replay", "/x", "--replay-speed", "abc"}).has_value());
    EXPECT_FALSE(parse({"--replay"}).has_value());
}

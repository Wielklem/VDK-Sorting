#include <initializer_list>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "record_cli.hpp"

using namespace std::string_view_literals;
using namespace vsort;
using namespace vsort::record;

namespace {

Result<Options> parse(std::initializer_list<std::string_view> args) {
    const std::vector<std::string_view> list{args};
    return parseArgs(list);
}

void expectRejected(std::initializer_list<std::string_view> args) {
    const auto r = parse(args);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Errc::InvalidArgument);
}

} // namespace

TEST(RecordCli, HelpAndVersionNeedNoCamera) {
    const auto help = parse({"--help"sv});
    ASSERT_TRUE(help.has_value());
    EXPECT_TRUE(help->help);

    const auto version = parse({"--version"sv});
    ASSERT_TRUE(version.has_value());
    EXPECT_TRUE(version->version);
}

TEST(RecordCli, DefaultsWithOneCamera) {
    const auto r = parse({"--camera"sv, "0=SN1"sv});
    ASSERT_TRUE(r.has_value()) << r.error().what();
    ASSERT_EQ(r->cameras.size(), 1U);
    EXPECT_EQ(r->cameras[0].index, 0);
    EXPECT_EQ(r->cameras[0].serial, "SN1");
    EXPECT_TRUE(r->outDir.empty());
    EXPECT_EQ(r->trigger, camera::TriggerMode::Hardware);
    EXPECT_DOUBLE_EQ(r->exposureUs, 10000.0);
    EXPECT_DOUBLE_EQ(r->gainDb, 0.0);
    EXPECT_EQ(r->duration.count(), 0);
    EXPECT_EQ(r->framesPerCamera, 0U);
    EXPECT_EQ(r->queueDepth, 32U);
}

TEST(RecordCli, ParsesAllOptions) {
    const auto r = parse({"--camera"sv, "0=SN0"sv, "--camera=1=SN1"sv, "--out"sv, "data"sv,
                          "--label"sv, "first run"sv, "--duration"sv, "30"sv, "--frames=500"sv,
                          "--exposure-us"sv, "2500.5"sv, "--gain-db=3"sv, "--trigger"sv,
                          "freerun"sv, "--queue-depth"sv, "64"sv, "--log-level"sv, "debug"sv});
    ASSERT_TRUE(r.has_value()) << r.error().what();
    ASSERT_EQ(r->cameras.size(), 2U);
    EXPECT_EQ(r->cameras[1].index, 1);
    EXPECT_EQ(r->cameras[1].serial, "SN1");
    EXPECT_EQ(r->outDir, std::filesystem::path{"data"});
    EXPECT_EQ(r->label, "first run");
    EXPECT_EQ(r->duration.count(), 30);
    EXPECT_EQ(r->framesPerCamera, 500U);
    EXPECT_DOUBLE_EQ(r->exposureUs, 2500.5);
    EXPECT_DOUBLE_EQ(r->gainDb, 3.0);
    EXPECT_EQ(r->trigger, camera::TriggerMode::FreeRun);
    EXPECT_EQ(r->queueDepth, 64U);
    EXPECT_EQ(r->logLevel, log::Level::Debug);
}

TEST(RecordCli, NeedsAtLeastOneCamera) {
    expectRejected({});
    expectRejected({"--frames"sv, "10"sv});
}

TEST(RecordCli, RejectsBadCameraSpecs) {
    expectRejected({"--camera"sv, "SN1"sv});
    expectRejected({"--camera"sv, "=SN1"sv});
    expectRejected({"--camera"sv, "0="sv});
    expectRejected({"--camera"sv, "x=SN1"sv});
    expectRejected({"--camera"sv, "-1=SN1"sv});
    expectRejected({"--camera"sv, "70000=SN1"sv});
    expectRejected({"--camera"sv, "0=A"sv, "--camera"sv, "0=B"sv});
    expectRejected({"--camera"sv, "0=A"sv, "--camera"sv, "1=A"sv});
}

TEST(RecordCli, RejectsBadValues) {
    expectRejected({"--camera"sv, "0=A"sv, "--bogus"sv});
    expectRejected({"--camera"sv, "0=A"sv, "--out"sv});
    expectRejected({"--camera"sv, "0=A"sv, "--trigger"sv, "software"sv});
    expectRejected({"--camera"sv, "0=A"sv, "--exposure-us"sv, "0"sv});
    expectRejected({"--camera"sv, "0=A"sv, "--exposure-us"sv, "abc"sv});
    expectRejected({"--camera"sv, "0=A"sv, "--gain-db"sv, "-1"sv});
    expectRejected({"--camera"sv, "0=A"sv, "--queue-depth"sv, "0"sv});
    expectRejected({"--camera"sv, "0=A"sv, "--queue-depth"sv, "5000"sv});
    expectRejected({"--camera"sv, "0=A"sv, "--frames"sv, "-1"sv});
    expectRejected({"--camera"sv, "0=A"sv, "--duration"sv, "-5"sv});
    expectRejected({"--camera"sv, "0=A"sv, "--duration"sv, "99999999"sv});
    expectRejected({"--camera"sv, "0=A"sv, "--log-level"sv, "loud"sv});
}

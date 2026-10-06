#include <array>
#include <string_view>

#include <gtest/gtest.h>

#include "cli.hpp"

using namespace std::string_view_literals;
using namespace vsort::service;

TEST(Cli, Defaults) {
    const auto r = parseArgs({});
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r->help);
    EXPECT_FALSE(r->check);
    EXPECT_TRUE(r->root.empty());
    EXPECT_EQ(r->logLevel, vsort::log::Level::Info);
    EXPECT_EQ(r->scope, vsort::platform::PathScope::System);
}

TEST(Cli, ParsesOptions) {
    const std::array args{"--root"sv, "/tmp/x"sv, "--log-level=debug"sv,
                          "--check"sv, "--no-console"sv, "--user"sv};
    const auto r = parseArgs(args);
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(r->root, std::filesystem::path{"/tmp/x"});
    EXPECT_EQ(r->logLevel, vsort::log::Level::Debug);
    EXPECT_TRUE(r->check);
    EXPECT_TRUE(r->noConsole);
    EXPECT_EQ(r->scope, vsort::platform::PathScope::User);
}

TEST(Cli, RejectsBadInput) {
    const std::array unknown{"--bogus"sv};
    EXPECT_FALSE(parseArgs(unknown).has_value());

    const std::array missing{"--root"sv};
    EXPECT_FALSE(parseArgs(missing).has_value());

    const std::array badLevel{"--log-level"sv, "loud"sv};
    EXPECT_FALSE(parseArgs(badLevel).has_value());
}

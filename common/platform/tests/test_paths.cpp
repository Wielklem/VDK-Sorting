#include <filesystem>

#include <gtest/gtest.h>

#include <vsort/platform/paths.hpp>

#include "test_util.hpp"

using vsort::platform::PathScope;

TEST(Paths, RootedLayout) {
    const auto root = std::filesystem::temp_directory_path() / "vsort_paths_test";
    std::filesystem::remove_all(root);

    const auto paths = vsort::platform::makeRootedPaths(root);
    EXPECT_EQ(paths->configDir(), root / "config");
    EXPECT_EQ(paths->dataDir(), root / "data");
    EXPECT_EQ(paths->logDir(), root / "log");

    ASSERT_TRUE(vsort::platform::ensureDirectories(*paths).has_value());
    EXPECT_TRUE(std::filesystem::is_directory(paths->configDir()));
    EXPECT_TRUE(std::filesystem::is_directory(paths->logDir()));

    std::filesystem::remove_all(root);
}

TEST(Paths, StandardUserScope) {
    const auto paths = vsort::platform::makeStandardPaths(PathScope::User);
    VSORT_SKIP_IF_NOT_SUPPORTED(paths);
    ASSERT_TRUE(paths.has_value()) << paths.error().what();
    EXPECT_TRUE((*paths)->configDir().is_absolute());
    EXPECT_EQ((*paths)->configDir().filename(), "vsort");
    EXPECT_NE((*paths)->dataDir(), (*paths)->logDir());
}

TEST(Paths, StandardSystemScope) {
    const auto paths = vsort::platform::makeStandardPaths(PathScope::System);
    VSORT_SKIP_IF_NOT_SUPPORTED(paths);
    ASSERT_TRUE(paths.has_value()) << paths.error().what();
    EXPECT_TRUE((*paths)->configDir().is_absolute());
    EXPECT_TRUE((*paths)->dataDir().is_absolute());
    EXPECT_TRUE((*paths)->logDir().is_absolute());
}

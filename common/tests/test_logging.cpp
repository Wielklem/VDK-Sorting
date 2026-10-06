#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include <vsort/common/logging.hpp>

TEST(Logging, WritesRotatingFile) {
    const auto dir = std::filesystem::temp_directory_path() / "vsort_log_test";
    std::filesystem::remove_all(dir);

    vsort::log::Config cfg;
    cfg.directory = dir;
    cfg.console = false;
    ASSERT_TRUE(vsort::log::init(cfg).has_value());

    auto logger = vsort::log::get("test");
    logger->info("hello {}", 42);
    logger->flush();
    logger.reset();          // drop our reference, otherwise the file stays open
    vsort::log::shutdown();  // close files first (Windows can't delete open files)

    std::string content;
    {
        std::ifstream in{dir / "vsort.log"};
        ASSERT_TRUE(in.is_open());
        std::stringstream ss;
        ss << in.rdbuf();
        content = ss.str();
    }
    EXPECT_NE(content.find("hello 42"), std::string::npos);
    EXPECT_NE(content.find("[test]"), std::string::npos);

    std::filesystem::remove_all(dir);
}

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include <vsort/replay/replay_session.hpp>

using namespace vsort;
using namespace vsort::replay;

namespace {

class ReplaySessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() / "vsort_replay_session_test";
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
    }
    void TearDown() override { std::filesystem::remove_all(dir_); }

    void write(const std::string& text) const {
        std::ofstream out{dir_ / "session.json"};
        out << text;
    }

    std::filesystem::path dir_;
};

} // namespace

TEST_F(ReplaySessionTest, ListsCamerasSortedByIndex) {
    write(R"({"cameras": [
      {"index": 3, "serial": "SN-C", "model": "M3", "file": "cam03.vrec"},
      {"index": 1, "serial": "SN-A", "model": "M1", "file": "cam01.vrec"}]})");
    const auto list = listSessionCameras(dir_);
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list->size(), 2U);
    EXPECT_EQ((*list)[0].serial, "SN-A");
    EXPECT_EQ((*list)[0].index, 1U);
    EXPECT_EQ((*list)[1].serial, "SN-C");
    EXPECT_EQ((*list)[1].model, "M3");
}

TEST_F(ReplaySessionTest, MissingFileIsNotFound) {
    const auto list = listSessionCameras(dir_ / "nowhere");
    ASSERT_FALSE(list.has_value());
    EXPECT_EQ(list.error().code, Errc::NotFound);
}

TEST_F(ReplaySessionTest, DamagedFileIsAParseError) {
    write("{ not json");
    const auto list = listSessionCameras(dir_);
    ASSERT_FALSE(list.has_value());
    EXPECT_EQ(list.error().code, Errc::ParseError);
}

TEST_F(ReplaySessionTest, CameraWithoutSerialIsAParseError) {
    write(R"({"cameras": [{"index": 0, "model": "M"}]})");
    const auto list = listSessionCameras(dir_);
    ASSERT_FALSE(list.has_value());
    EXPECT_EQ(list.error().code, Errc::ParseError);
}
